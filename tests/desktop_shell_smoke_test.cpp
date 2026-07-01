#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QTime>
#include <QUrl>
#include <QVariant>
#include <qqml.h>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QVector>

#include "app_shell_controller.h"
#include "brush_live_preview_item.h"
#include "carousel_document_controller.h"
#include "catalog_asset_model.h"
#include "editor_preview_controller.h"
#include "editor_preview_texture_item.h"
#include "export_job_controller.h"
#include "job_queue_model.h"
#include "job_store.h"
#include "job_runner.h"
#include "macos_updater.h"
#include "preview_backend_controller.h"
#include "tablet_input_area.h"
#include "timeline_document_controller.h"
#include "timeline_playback_controller.h"

namespace photo_editor::desktop {

struct EditorPreviewControllerTestAccess {
  static int render_generation(const EditorPreviewController& controller) {
    return controller.render_generation_;
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
  constexpr int width = 1200;
  constexpr int height = 800;
  const auto kind = fixture_kind.trimmed().toLower();
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
      if (kind == QStringLiteral("bright")) {
        const auto value = static_cast<int>(230.0 + 24.0 * fx + 10.0 * fy);
        color = QColor(qBound(0, value, 255), qBound(0, value + 2, 255), qBound(0, value + 6, 255));
      } else if (kind == QStringLiteral("dark")) {
        const auto value = static_cast<int>(8.0 + 24.0 * fx + 16.0 * fy);
        color = QColor(qBound(0, value, 255), qBound(0, value + 2, 255), qBound(0, value + 5, 255));
      } else if (kind == QStringLiteral("saturated")) {
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
  if (auto* item = qobject_cast<QQuickItem*>(object); item != nullptr) {
    return item_visible(item);
  }
  const auto visible = object->property("visible");
  return !visible.isValid() || visible.toBool();
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
  const auto image = window->grabWindow();
  if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
    std::cerr << "Visual proof state " << state.toStdString() << " produced a blank capture\n";
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

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));

  QApplication app(argc, argv);
  QQuickStyle::setStyle(QStringLiteral("Basic"));
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("DesktopShellSmokeTest"));

  QTemporaryDir runtime_root;
  QTemporaryDir cache_root;
  if (!runtime_root.isValid() || !cache_root.isValid()) {
    return fail("temporary runtime directories should be created");
  }

  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", cache_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", QByteArray("postgres://example/test"));

  photo_editor::desktop::AppShellController app_shell;
  photo_editor::desktop::CatalogAssetModel catalog_assets;
  photo_editor::desktop::CarouselDocumentController carousel_document;
  photo_editor::desktop::EditorPreviewController editor_preview;
  photo_editor::desktop::JobRunner job_runner;
  photo_editor::desktop::JobQueueModel job_queue;
  photo_editor::desktop::ExportJobController export_jobs;
  photo_editor::desktop::MacosUpdater macos_updater;
  photo_editor::desktop::PreviewBackendController preview_backend;
  photo_editor::desktop::TimelineDocumentController timeline_document;
  photo_editor::desktop::TimelinePlaybackController timeline_playback(&timeline_document);

  preview_backend.setPreferredBackend(QStringLiteral("CPU"));
  if (preview_backend.activeBackend() != QStringLiteral("CPU")) {
    return fail("CPU preview preference should force the active preview backend to CPU");
  }
  if (!preview_backend.fallbackReason().isEmpty()) {
    return fail("CPU preview preference should not report a GPU fallback reason");
  }
  preview_backend.setPreferredBackend(QStringLiteral("GPU Preferred"));
  if (preview_backend.gpuAvailable()) {
    if (preview_backend.activeBackend() == QStringLiteral("CPU")) {
      return fail("GPU Preferred should use an available GPU preview backend");
    }
  } else {
    if (preview_backend.activeBackend() != QStringLiteral("CPU")) {
      return fail("GPU Preferred should fall back to CPU when no GPU preview backend is available");
    }
    if (preview_backend.fallbackReason().trimmed().isEmpty()) {
      return fail("GPU Preferred CPU fallback should expose an unavailable-backend reason");
    }
  }
  preview_backend.setPreferredBackend(QStringLiteral("Auto"));
  const auto mask_memory_policy = editor_preview.maskMemoryPolicySignature();
  if (!mask_memory_policy.contains(QStringLiteral("foreground-mattes:67108864")) ||
      !mask_memory_policy.contains(QStringLiteral("mask-tiles:134217728")) ||
      !mask_memory_policy.contains(QStringLiteral("mask-thumbnails:524288")) ||
      !mask_memory_policy.contains(QStringLiteral("inpaint-patch:100663296")) ||
      !mask_memory_policy.contains(QStringLiteral("inpaint-candidates:33554432:5")) ||
      !mask_memory_policy.contains(QStringLiteral("embeddings:0"))) {
    return fail("desktop shell should expose the mask memory policy for generated mattes, thumbnails, inpaint patches, candidates, and embeddings");
  }

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

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("appShell", &app_shell);
  engine.rootContext()->setContextProperty("catalogAssets", &catalog_assets);
  engine.rootContext()->setContextProperty("carouselDocument", &carousel_document);
  engine.rootContext()->setContextProperty("editorPreview", &editor_preview);
  engine.rootContext()->setContextProperty("exportJobs", &export_jobs);
  engine.rootContext()->setContextProperty("jobQueue", &job_queue);
  engine.rootContext()->setContextProperty("macosUpdater", &macos_updater);
  engine.rootContext()->setContextProperty("previewBackend", &preview_backend);
  engine.rootContext()->setContextProperty("timelineDocument", &timeline_document);
  engine.rootContext()->setContextProperty("timelinePlayback", &timeline_playback);

  const auto main_qml_path =
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)).filePath(QStringLiteral("apps/desktop/qml/Main.qml"));
  QFile mask_view_qml(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                           .filePath(QStringLiteral("apps/desktop/qml/workspaces/MaskView.qml")));
  if (!mask_view_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect MaskView generated-mask labels");
  }
  const auto mask_view_source = QString::fromUtf8(mask_view_qml.readAll());
  QFile studio_workspace_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/workspaces/StudioWorkspace.qml")));
  if (!studio_workspace_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect StudioWorkspace selection labels");
  }
  const auto studio_workspace_source = QString::fromUtf8(studio_workspace_qml.readAll());
  QFile edit_view_qml(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                          .filePath(QStringLiteral("apps/desktop/qml/workspaces/EditView.qml")));
  if (!edit_view_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect EditView mask accent labels");
  }
  const auto edit_view_source = QString::fromUtf8(edit_view_qml.readAll());
  QFile mask_inspector_qml(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                               .filePath(QStringLiteral(
                                   "apps/desktop/qml/workspaces/MaskInspectorSections.qml")));
  if (!mask_inspector_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect mask inspector selection labels");
  }
  const auto mask_inspector_source = QString::fromUtf8(mask_inspector_qml.readAll());
  QFile mask_canvas_overlay_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/workspaces/MaskCanvasOverlay.qml")));
  if (!mask_canvas_overlay_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect mask canvas overlay controls");
  }
  const auto mask_canvas_overlay_source = QString::fromUtf8(mask_canvas_overlay_qml.readAll());
  QFile editor_preview_controller_cpp(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/src/editor_preview_controller.cpp")));
  if (!editor_preview_controller_cpp.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect preview scheduler contracts");
  }
  const auto editor_preview_controller_source =
      QString::fromUtf8(editor_preview_controller_cpp.readAll());
  QFile job_runner_cpp(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                           .filePath(QStringLiteral("apps/desktop/src/job_runner.cpp")));
  if (!job_runner_cpp.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect export telemetry contracts");
  }
  const auto job_runner_source = QString::fromUtf8(job_runner_cpp.readAll());
  QFile shared_mask_overlay_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/workspaces/SharedMaskOverlay.qml")));
  if (!shared_mask_overlay_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect the shared mask overlay");
  }
  const auto shared_mask_overlay_source = QString::fromUtf8(shared_mask_overlay_qml.readAll());
  if (!mask_canvas_overlay_source.contains(
          QStringLiteral("likely edge, shadow, or reflection pixels"))) {
    return fail("object remove canvas guidance should explain assisted shadow/reflection expansion");
  }
  if (!studio_workspace_source.contains(QStringLiteral("SharedMaskOverlay {")) ||
      !mask_view_source.contains(QStringLiteral("SharedMaskOverlay {")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("objectNamePrefix")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("sharedOverlayVersion")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("overlayGeometryContract")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("overlayGeometrySignature")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("maskRasterPreviewPolicy")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("maskPreviewMode")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("GeometricMaskCanvas")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("polygonVertexHandle")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("polygonEdgeInsertHandle")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("handleReadabilityContract")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("handleOuterHaloColor")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("handleInnerHaloColor")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("anchors.margins: -8"))) {
    return fail("StudioWorkspace and MaskView should share one mask overlay renderer with stable matte, geometry, and polygon handle anchors");
  }
  if (studio_workspace_source.contains(QStringLiteral("ctx.lineTo(x + height, 0)")) ||
      studio_workspace_source.contains(QStringLiteral("#D4FFF8"))) {
    return fail("mask and brush modes should not draw diagonal hatch lines over the image");
  }
  const QRegularExpression photo_layer_opacity_coalescing_contract(
      QStringLiteral("objectName:\\s*\\\"photoLayerOpacitySlider\\\"[\\s\\S]*"
                     "onPressedChanged:\\s*\\{[\\s\\S]*beginCoalescedEdit\\(\\)[\\s\\S]*"
                     "endCoalescedEdit\\(\\)[\\s\\S]*"
                     "onMoved:\\s*editorPreview\\.setPhotoLayerOpacity"));
  if (!photo_layer_opacity_coalescing_contract.match(studio_workspace_source).hasMatch()) {
    return fail("photo layer opacity drags should open a coalesced edit so one drag creates one undo step");
  }
  if (!editor_preview_controller_source.contains(
          QStringLiteral("layer-mask-render-stage-telemetry-v1")) ||
      !editor_preview_controller_source.contains(QStringLiteral("layer_recipe_evaluation_ms")) ||
      !editor_preview_controller_source.contains(QStringLiteral("mask_alpha_retrieval_ms")) ||
      !editor_preview_controller_source.contains(QStringLiteral("layer_composite_ms")) ||
      !editor_preview_controller_source.contains(QStringLiteral("thumbnail_layer_composite_ms")) ||
      !job_runner_source.contains(QStringLiteral("final_export_stage")) ||
      !job_runner_source.contains(QStringLiteral("final_export_ms")) ||
      !job_runner_source.contains(QStringLiteral("mask_alpha_retrieval_provider"))) {
    return fail("preview, thumbnail, and export logs should expose layer/mask stage telemetry fields");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function previewFailureRowLabel")) ||
      !studio_workspace_source.contains(QStringLiteral("function photoLayerPreviewFailureStage")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskSetPreviewFailureStage")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.previewFailureLayerIds")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.previewFailureMaskSetIds")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerFailureLabel")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskFailureLabel")) ||
      !studio_workspace_source.contains(QStringLiteral("layerFailureLabel + \" / \" + layerStateSummary")) ||
      !studio_workspace_source.contains(QStringLiteral("text: maskFailureLabel.length > 0 ? maskFailureLabel"))) {
    return fail("layer and mask rows should surface preview failure stages while preserving the last good canvas");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("id: previewItem")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("x: root.imageX")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("y: root.imageY")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("width: root.imageWidth")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("height: root.imageHeight")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("clip: true")) ||
      !studio_workspace_source.contains(QStringLiteral("x: viewport.paintedContentWidth > 1 ? viewport.paintedContentX")) ||
      !studio_workspace_source.contains(QStringLiteral("width: viewport.paintedContentWidth > 1 ? viewport.paintedContentWidth")) ||
      !studio_workspace_source.contains(QStringLiteral("imageX: editViewport.paintedContentX")) ||
      !studio_workspace_source.contains(QStringLiteral("imageWidth: editViewport.paintedContentWidth")) ||
      !edit_view_source.contains(QStringLiteral("imageX: editorPreview.displayPreviewNativeAvailable ? editedPreviewFitNative.x")) ||
      !edit_view_source.contains(QStringLiteral("imageWidth: editorPreview.displayPreviewNativeAvailable ? Math.max(0, editedPreviewFitNative.width)"))) {
    return fail("mask overlays and canvas background treatment should be clipped to the painted image rect, leaving pasteboard treatment outside image pixels");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("outlineRenderContract")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("overlayGeometryContract")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("overlayGeometrySignature")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("clip: true")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("stableOutlineWidth")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("stableOutlineDash")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("normalizedOutlinePathsCache")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("outlineQualityLadderContract")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("activeMaskOutlineQualityTier")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("outlineTraceContract: \"public-outline-trace|source|tolerance|provider|path-count|image-normalized|final-alpha:v2\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("readonly property var activeOutlineTrace: editorPreview.activeMaskOutlineTrace || ({})")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("readonly property real outlineTraceTolerancePixels: root.activeOutlineTraceTolerancePixels")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("readonly property string outlineCoordinateSpace: root.activeOutlineCoordinateSpace")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("readonly property string outlineAlphaSourceContract: root.activeOutlineAlphaSourceContract")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("readonly property string outlineTraceProviderContract: root.activeOutlineTraceProviderContract")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("readonly property string outlineTraceMaskAlphaSignature: root.activeOutlineTraceMaskAlphaSignature")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskOutlineQualityLabel")) ||
      !mask_inspector_source.contains(QStringLiteral("function maskOutlineQualityLabel")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("pixelAligned")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("antialiasing: true")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("outlinePhaseAnimationEnabled")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("onSelectionOutlineAnimationEnabledChanged")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("handleReadabilityContract")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("handleOuterHaloColor")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("handleInnerHaloColor")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("outlineRenderContract")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("outlineAnimationContract")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("outlineTraceContract: \"public-outline-trace|source|tolerance|provider|path-count|image-normalized|final-alpha:v2\"")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("readonly property var activeOutlineTrace: editorPreview.activeMaskOutlineTrace || ({})")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("readonly property real outlineTraceTolerancePixels: root.activeOutlineTraceTolerancePixels")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("readonly property string outlineCoordinateSpace: root.activeOutlineCoordinateSpace")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("readonly property string outlineAlphaSourceContract: root.activeOutlineAlphaSourceContract")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("readonly property string outlineTraceProviderContract: root.activeOutlineTraceProviderContract")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("readonly property string outlineTraceMaskAlphaSignature: root.activeOutlineTraceMaskAlphaSignature")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("SharedActiveSelectionOutline")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("outlineAnimationEnabled")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("sharedCanvasPaintTimer")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("scheduleActiveSelectionOutlinePaint")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("stableOutlineWidth")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("stableOutlineDash")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("normalizedOutlinePathsCache")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("pixelAligned")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("antialiasing: true")) ||
      !studio_workspace_source.contains(QStringLiteral("selectionOutlineAnimationContract")) ||
      !studio_workspace_source.contains(QStringLiteral("selectionOutlineAnimationPointBudget")) ||
      !studio_workspace_source.contains(QStringLiteral("selectionOutlineAnimationShouldRun")) ||
      !studio_workspace_source.contains(QStringLiteral("if (!root.visible || page !== \"edit\"")) ||
      !studio_workspace_source.contains(QStringLiteral("pointCount >= 2 && pointCount <= root.selectionOutlineAnimationPointBudget")) ||
      !studio_workspace_source.contains(QStringLiteral("cropX: editorPreview.cropRectX")) ||
      !studio_workspace_source.contains(QStringLiteral("transformRotation: editorPreview.transformRotation")) ||
      !mask_view_source.contains(QStringLiteral("maskViewActiveSelectionOutline")) ||
      !mask_view_source.contains(QStringLiteral("outlineRenderContract")) ||
      !mask_view_source.contains(QStringLiteral("cropX: editorPreview.cropRectX")) ||
      !mask_view_source.contains(QStringLiteral("transformRotation: editorPreview.transformRotation")) ||
      !mask_view_source.contains(QStringLiteral("selectionOutlineAnimationContract")) ||
      !mask_view_source.contains(QStringLiteral("selectionOutlineAnimationPointBudget")) ||
      !mask_view_source.contains(QStringLiteral("selectionOutlineAnimationShouldRun")) ||
      !mask_view_source.contains(QStringLiteral("if (!root.visible || (!editorPreview.hasActiveMask")) ||
      !mask_view_source.contains(QStringLiteral("pointCount >= 2 && pointCount <= root.selectionOutlineAnimationPointBudget")) ||
      !editor_preview_controller_source.contains(QStringLiteral("kMaxTracedOutlinePaths")) ||
      !editor_preview_controller_source.contains(QStringLiteral("kMaxTracedOutlinePoints")) ||
      !editor_preview_controller_source.contains(QStringLiteral("kMinSecondaryOutlineAreaRatio")) ||
      !editor_preview_controller_source.contains(QStringLiteral("minimum_secondary_area")) ||
      !mask_view_source.contains(QStringLiteral("handleReadabilityContract")) ||
      !mask_view_source.contains(QStringLiteral("handleOuterHaloColor")) ||
      !mask_view_source.contains(QStringLiteral("handleInnerHaloColor")) ||
      !mask_view_source.contains(QStringLiteral("stableOutlineWidth")) ||
      !mask_view_source.contains(QStringLiteral("stableOutlineDash")) ||
      !mask_view_source.contains(QStringLiteral("pixelAligned")) ||
      !mask_view_source.contains(QStringLiteral("antialiasing: true")) ||
      !edit_view_source.contains(QStringLiteral("selectionOutlineAnimationContract")) ||
      !edit_view_source.contains(QStringLiteral("selectionOutlineAnimationPointBudget")) ||
      !edit_view_source.contains(QStringLiteral("selectionOutlineAnimationShouldRun")) ||
      !edit_view_source.contains(QStringLiteral("if (!root.visible || (!editorPreview.hasActiveMask")) ||
      !edit_view_source.contains(QStringLiteral("pointCount >= 2 && pointCount <= root.selectionOutlineAnimationPointBudget"))) {
    return fail("mask outlines should declare anti-aliased, pixel-aligned, zoom-stable rendering and gated animation contracts");
  }
  if (!editor_preview_controller_source.contains(QStringLiteral("QVariantMap EditorPreviewController::activeMaskOutlineTrace() const")) ||
      !editor_preview_controller_source.contains(QStringLiteral("(void)activeMaskOutlinePaths();")) ||
      !editor_preview_controller_source.contains(QStringLiteral("return active_mask_outline_trace_fields_;"))) {
    return fail("active mask outline trace metadata should be exposed through the public controller API");
  }
  if (!edit_view_source.contains(QStringLiteral("maskPreviewDisplayCycleContract: \"legacy-editview-mask-preview-cycle|overlay|outline|marching-ants|hidden:v1\"")) ||
      !edit_view_source.contains(QStringLiteral("if (editorPreview.maskPreviewDisplayMode === \"overlay\")")) ||
      !edit_view_source.contains(QStringLiteral("editorPreview.maskPreviewMode = \"outline\"")) ||
      !edit_view_source.contains(QStringLiteral("else if (editorPreview.maskPreviewDisplayMode === \"outline\")")) ||
      !edit_view_source.contains(QStringLiteral("else if (editorPreview.maskPreviewDisplayMode === \"marching-ants\")")) ||
      !edit_view_source.contains(QStringLiteral("editorPreview.maskPreviewMode = \"hidden\""))) {
    return fail("legacy EditView mask preview shortcut should cycle through public display mode names");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("maskCanvasHoverInputHandler")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("maskCanvasBrushStrokeTabletInput")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("maskCanvasBrushStrokeMouseInput")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("maskCanvasSelectionDragMouseInput")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("brush-resize")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smart-selection-drag")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("lasso-drag")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("pointerGrabPolicy: \"exclusive-brush-stroke\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("pointerGrabPolicy: \"exclusive-selection-drag\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("preventStealing: true")) ||
      !studio_workspace_source.contains(QStringLiteral("PanZoomInputHandler")) ||
      !studio_workspace_source.contains(QStringLiteral("inputHandlerRole: \"pan-zoom\"")) ||
      !studio_workspace_source.contains(QStringLiteral("pointerGrabPolicy: viewport.masked ? \"defer-to-mask-overlay\" : \"viewport-pan-zoom\"")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskSpacePanShortcutActive()")) ||
      !studio_workspace_source.contains(QStringLiteral("event.key === Qt.Key_Space")) ||
      !studio_workspace_source.contains(QStringLiteral("root.spacePanActive = true")) ||
      !studio_workspace_source.contains(QStringLiteral("!root.spacePanActive &&")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskOperationForModifierState(modifierState)")) ||
      !studio_workspace_source.contains(QStringLiteral("function applyMaskOperationForModifierState(modifierState)")) ||
      !studio_workspace_source.contains(QStringLiteral("Qt.ShiftModifier")) ||
      !studio_workspace_source.contains(QStringLiteral("Qt.AltModifier")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editRoot.applyMaskOperationForModifierState(mouse.modifiers)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editRoot.applyObjectPickModeForModifierState(mouse.modifiers)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("enabled: active && !editRoot.spacePanActive")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("enabled: !editRoot.spacePanActive &&")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("polygonEdgeInsertInputHandler")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("polygonVertexDragInputHandler")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("inputHandlerRole: \"polygon-edit\"")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("pointerGrabPolicy: \"exclusive-polygon-edit\"")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("preventStealing: true")) ||
      !mask_view_source.contains(QStringLiteral("pointerGrabPolicy: \"exclusive-smart-selection-stroke\"")) ||
      !mask_view_source.contains(QStringLiteral("pointerGrabPolicy: \"exclusive-lasso-drag\"")) ||
      !mask_view_source.contains(QStringLiteral("pointerGrabPolicy: \"exclusive-polygon-tap\"")) ||
      !mask_view_source.contains(QStringLiteral("grabPermissions: PointerHandler.TakeOverForbidden"))) {
    return fail("masking input paths should expose separate hover, tap, drag, brush stroke, polygon edit, resize, pan/zoom, and modifier operation roles");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("function brushCursorState(mask)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function selectionCursorState()")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function cursorShapeForState(cursorState)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("return \"clone-source\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("return \"retouch-apply\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("return editorPreview.activeMaskOperation === \"subtract\" ? \"erase\" : \"add\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("return \"subtract\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("? \"close-polygon\" : \"polygon-point\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("readonly property string cursorState: root.brushCursorState(mask)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("readonly property string cursorState: root.selectionCursorState()"))) {
    return fail("masking cursor states should distinguish add, subtract, erase, polygon point, close polygon, clone source, and retouch apply");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("smartSelectHoverPrewarmContract: \"pointer-pause|jitter-gated|no-enter-prewarm:v1\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("id: smartSelectPreviewPauseTimer")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewPauseMs: 180")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewMoveEpsilon: 0.003")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewPauseTimer.restart()")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("root.stopPendingSmartSelectPreview()")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("root.scheduleSmartSelectPreviewDebounced(mouse.x / previewItem.width, mouse.y / previewItem.height, false)")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("root.scheduleSmartSelectPreviewDebounced(mouse.x / previewItem.width, mouse.y / previewItem.height, true)"))) {
    return fail("smart-selection hover prewarm should wait for pointer pause, ignore jitter, and avoid immediate enter/release segmentation");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("maskCanvasBrushCursorFeatherRing")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("maskCanvasBrushCursorOuterHaloRing")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("maskCanvasBrushCursorHardnessRing")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("maskCanvasBrushCursorReadout")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("brushCursorVisibilityContract: \"persistent-last-position|dual-contrast-ring|hardness-inner-ring:v1\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("brushCursorRepaintContract: \"cursor-item-position-binding|no-static-canvas-repaint-on-hover:v1\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("liveOverlayFrameBudgetMs: 16")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("liveOverlayPointBudget: 512")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("live-brush-overlay|ui-frame-budget:16ms|point-budget:512|no-preview-render-block:v1")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("const paintStart = Math.max(0, points.length - paintCount)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("brushCursorPositionKnown")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("visible: ((brushMode && cursorPositionReady)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("x: root.hoverX - width / 2")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("y: root.hoverY - height / 2")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("\"  Flow \" + brushPercentText(editorPreview.activeMaskBrushFlow")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("\"  Density \" + brushPercentText(editorPreview.activeMaskDensity")) ||
      !mask_view_source.contains(QStringLiteral("maskViewBrushCursorOuterHaloRing")) ||
      !mask_view_source.contains(QStringLiteral("maskViewBrushCursorFeatherRing")) ||
      !mask_view_source.contains(QStringLiteral("maskViewBrushCursorHardnessRing")) ||
      !mask_view_source.contains(QStringLiteral("maskViewBrushCursorReadout")) ||
      !mask_view_source.contains(QStringLiteral("brushCursorVisibilityContract: \"persistent-last-position|dual-contrast-ring|hardness-inner-ring:v1\"")) ||
      !mask_view_source.contains(QStringLiteral("brushCursorPositionKnown")) ||
      !mask_view_source.contains(QStringLiteral("\"  Flow \" + root.percentText(editorPreview.activeMaskBrushFlow)")) ||
      !mask_view_source.contains(QStringLiteral("\"  Density \" + root.percentText(editorPreview.activeMaskDensity)")) ||
      !studio_workspace_source.contains(QStringLiteral("function brushDensityValueText()")) ||
      !studio_workspace_source.contains(QStringLiteral("function brushOpacityValueText()")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"brushContextCompactBrushPanel\"")) ||
      !studio_workspace_source.contains(QStringLiteral("brushPanelContract: \"size-flow-hardness-density-opacity|compact:v1\"")) ||
      !studio_workspace_source.contains(QStringLiteral("BrushCursorOuterHaloRing")) ||
      !studio_workspace_source.contains(QStringLiteral("brushCursorVisibilityContract: \"persistent-last-position|dual-contrast-ring|hardness-inner-ring:v1\"")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: root.brushToolReady() && previewImage.visible")) ||
      studio_workspace_source.contains(QStringLiteral("visible: root.brushToolReady() && viewportMouse.containsMouse")) ||
      !studio_workspace_source.contains(QStringLiteral("BrushCursorFeatherRing")) ||
      !studio_workspace_source.contains(QStringLiteral("\" / Flow \" + root.brushFlowValueText()")) ||
      !studio_workspace_source.contains(QStringLiteral("\" / Density \" + root.brushDensityValueText()"))) {
    return fail("brush cursors and compact brush panels should expose feather, hardness, flow, density, and opacity readouts");
  }
  if (!shared_mask_overlay_source.contains(QStringLiteral("maskOverlayColorContract: \"single-solid-default|custom-color-override|retouch-exception:v1\"")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("readonly property color defaultMaskOverlayColor: \"#24dcc2\"")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("readonly property color activeOperationColor: resolvedOverlayColor(")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("defaultMaskOverlayColor)")) ||
      !editor_preview_controller_source.contains(QStringLiteral("QColor selection_operation_preview_color")) ||
      !editor_preview_controller_source.contains(QStringLiteral("return QColor(QStringLiteral(\"#24dcc2\"));")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function selectionOperationOverlayColor(operationName)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function resolvedMaskOverlayColor()")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("settledMaskOutlineContract: \"outline-only|fragment-filtered|no-fill:v1\"")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("settledMaskOutlineContract: \"outline-only|fragment-filtered|no-fill:v1\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function displayOutlinePathLargeEnough(points, isCandidate)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("function displayOutlinePathLargeEnough(points)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("readonly property color overlayAccent: root.resolvedMaskOverlayColor()")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("return resolvedMaskOverlayColor()")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("GradientStop { position: 0.0; color: Qt.alpha(overlayAccent")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("root.strokeOutlinePaths(ctx, outlinePaths, root.resolvedMaskOverlayColor(), false)")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("root.fillOutlinePaths(ctx, outlinePaths, overlayAccent, 0.18)")) ||
      shared_mask_overlay_source.contains(QStringLiteral("root.fillOutlinePaths(ctx, outlinePaths)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("return selectionOperationOverlayColor(\"subtract\")")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("root.selectionOperationOverlayColor(editorPreview.activeMaskOperation)")) ||
      !mask_view_source.contains(QStringLiteral("function selectionOperationOverlayColor(operationName)")) ||
      !mask_view_source.contains(QStringLiteral("function resolvedMaskOverlayColor(mask)")) ||
      !mask_view_source.contains(QStringLiteral("readonly property color overlayAccent: root.resolvedMaskOverlayColor(mask)")) ||
      !mask_view_source.contains(QStringLiteral("ctx.fillStyle = Qt.alpha(overlayAccent")) ||
      !edit_view_source.contains(QStringLiteral("function selectionOperationOverlayColor(operationName)")) ||
      !edit_view_source.contains(QStringLiteral("return selectionOperationOverlayColor(mask.operation)")) ||
      !studio_workspace_source.contains(QStringLiteral("function selectionOperationOverlayColor(operationName)")) ||
      !studio_workspace_source.contains(QStringLiteral("border.color: root.selectionOperationOverlayColor(editorPreview.activeMaskOperation)"))) {
    return fail("selection overlays should use one solid custom-overridable color for live/candidate drawing and outline-only fragment-filtered settled masks");
  }
  if (!mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayButton")) ||
      !mask_inspector_source.contains(QStringLiteral("Color Overlay")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewColorOnBwButton")) ||
      !mask_inspector_source.contains(QStringLiteral("Color on B&W")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewImageOnBwButton")) ||
      !mask_inspector_source.contains(QStringLiteral("Image on B&W")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewMatteButton")) ||
      !mask_inspector_source.contains(QStringLiteral("Grayscale Alpha")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOnBlackButton")) ||
      !mask_inspector_source.contains(QStringLiteral("Isolated Mask")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOnWhiteButton")) ||
      !mask_inspector_source.contains(QStringLiteral("On White")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOutlineButton")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskPreviewMode = \"outline\"")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewMarchingAntsButton")) ||
      !mask_inspector_source.contains(QStringLiteral("Marching Ants")) ||
      !mask_inspector_source.contains(QStringLiteral("grayscale-alpha")) ||
      !mask_inspector_source.contains(QStringLiteral("isolated-mask")) ||
      !mask_inspector_source.contains(QStringLiteral("hidden")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("maskRasterPreviewPolicy")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function cleanVectorMaskFillVisible()")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editorPreview.maskPreviewMode !== \"outline\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("source: \"\"")) ||
      shared_mask_overlay_source.contains(QStringLiteral("activeMaskPreviewSource")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("readonly property string previewSource: \"\"")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("editorPreview.maskPreviewMode === \"outline\"")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("MaskPreviewBwBackdrop")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("function previewSourceOpacity(activeBrushPainting)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("activeSelectionBwBackdrop")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function usesBlackWhiteImageContext()")) ||
      !mask_inspector_source.contains(QStringLiteral("normalMaskDisplayContract")) ||
      !mask_inspector_source.contains(QStringLiteral("normalMaskPreviewModeFlow")) ||
      !mask_inspector_source.contains(QStringLiteral("diagnosticMaskDisplayContract")) ||
      !mask_inspector_source.contains(QStringLiteral("property bool diagnosticMaskPreviewModesVisible: false")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewDiagnosticsToggleButton")) ||
      !mask_inspector_source.contains(QStringLiteral("diagnosticMaskPreviewModeFlow")) ||
      !mask_inspector_source.contains(QStringLiteral("visible: root.diagnosticMaskPreviewModesVisible")) ||
      edit_view_source.contains(QStringLiteral("editorPreview.maskPreviewMode = \"color-on-bw\"")) ||
      edit_view_source.contains(QStringLiteral("editorPreview.maskPreviewMode = \"image-on-bw\"")) ||
      !edit_view_source.contains(QStringLiteral("editorPreview.maskPreviewMode = \"marching-ants\"")) ||
      !studio_workspace_source.contains(QStringLiteral("if (editorPreview.maskPreviewDisplayMode !== \"marching-ants\")")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.maskPreviewMode = editorPreview.maskPreviewDisplayMode === \"outline\" ? \"overlay\" : \"outline\"")) ||
      !studio_workspace_source.contains(QStringLiteral("root.setActiveMaskPreviewVisible(editorPreview.maskPreviewDisplayMode === \"hidden\")")) ||
      !studio_workspace_source.contains(QStringLiteral("\"|preview:\" + String(editorPreview.maskPreviewDisplayMode || \"overlay\")")) ||
      !studio_workspace_source.contains(QStringLiteral("const mode = String(editorPreview.maskPreviewDisplayMode || \"overlay\").trim().toLowerCase()")) ||
      !studio_workspace_source.contains(QStringLiteral("\"|mode:\" + String(editorPreview.maskPreviewDisplayMode || \"overlay\")")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.maskPreviewMode = visible ? \"overlay\" : \"hidden\"")) ||
      !studio_workspace_source.contains(QStringLiteral("return editorPreview.maskPreviewDisplayMode")) ||
      !studio_workspace_source.contains(QStringLiteral("active: editorPreview.maskPreviewDisplayMode !== \"hidden\"")) ||
      !studio_workspace_source.contains(QStringLiteral("active: editorPreview.maskPreviewDisplayMode === \"overlay\"")) ||
      !studio_workspace_source.contains(QStringLiteral("active: editorPreview.maskPreviewDisplayMode === \"outline\"")) ||
      !studio_workspace_source.contains(QStringLiteral("active: editorPreview.maskPreviewDisplayMode === \"hidden\"")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.maskPreviewMode = \"hidden\"")) ||
      studio_workspace_source.contains(QStringLiteral("editorPreview.maskPreviewMode = \"none\"")) ||
      !studio_workspace_source.contains(QStringLiteral("return \"Outline\"")) ||
      !studio_workspace_source.contains(QStringLiteral("return \"Grayscale Alpha\"")) ||
      !studio_workspace_source.contains(QStringLiteral("return \"Isolated Mask\""))) {
    return fail("mask preview controls should keep inspection modes explicit while the normal V shortcut cycles only overlay, marching ants, and hidden");
  }
  if (!mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayOpacitySlider")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskPreviewOverlayOpacity")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayColorDialog")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayColorSwatch")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayColorButton")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayColorResetButton")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewShowAllButton")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskPreviewShowAll = !editorPreview.maskPreviewShowAll")) ||
      !editor_preview_controller_source.contains(QStringLiteral("EditorPreviewController::maskPreviewShowAll() const")) ||
      !editor_preview_controller_source.contains(QStringLiteral("EditorPreviewController::setMaskPreviewShowAll(bool show_all)")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskPreviewOverlayColor = selectedColor")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("function resolvedOverlayColor(fallbackColor)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("function overlayOpacityScale()")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("Number(editorPreview.maskPreviewOverlayOpacity")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editorPreview.maskPreviewOverlayColor")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editorPreview.maskPreviewShowAll !== true"))) {
    return fail("mask preview controls should expose overlay opacity/color controls and an explicit show-all-masks toggle");
  }
  if (!studio_workspace_source.contains(QStringLiteral("activeInpaintRegenerationAvailable")) ||
      !studio_workspace_source.contains(QStringLiteral("regenerateActiveInpaintPatch()")) ||
      !studio_workspace_source.contains(QStringLiteral("Advance the seed for the pending retouch patch candidate"))) {
    return fail("retouch Regenerate should route committed patches through real inpaint regeneration while keeping pending seed control explicit");
  }
  if (!studio_workspace_source.contains(QStringLiteral("editorPreview.inpaintProgressActive")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.inpaintProgressValue")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.inpaintProgressLabel"))) {
    return fail("Studio should bind progressive final-resolution inpaint status into the retouch HUD and mask activity chip");
  }
  if (!mask_view_source.contains(QStringLiteral("editorPreview.inpaintPatchDiagnosticsAvailable")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.inpaintPatchDiagnosticsAvailable")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editorPreview.inpaintPatchDiagnosticsAvailable")) ||
      !mask_view_source.contains(QStringLiteral("Show Patch Bounds")) ||
      !mask_inspector_source.contains(QStringLiteral("Show Patch Bounds"))) {
    return fail("inpaint patch bounds diagnostics should exist only behind the developer/debug availability gate");
  }
  if (!mask_inspector_source.contains(QStringLiteral("activeMaskColorRangeSampleButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskColorRangeSampleCountLabel")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskColorRangeRemoveSampleButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskColorSamples")) ||
      !studio_workspace_source.contains(QStringLiteral("maskColorRangeSampling")) ||
      !studio_workspace_source.contains(QStringLiteral("sampleActiveColorRangeAt"))) {
    return fail("color range mask UI should expose persistent eyedropper samples and remove controls");
  }
  if (!mask_inspector_source.contains(QStringLiteral("activeMaskLuminanceRangeSampleButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskLuminanceRangeSampleCountLabel")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskLuminanceRangeRemoveSampleButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskLuminanceSamples")) ||
      !studio_workspace_source.contains(QStringLiteral("maskLuminanceRangeSampling")) ||
      !studio_workspace_source.contains(QStringLiteral("luminanceRangeSamplePatch")) ||
      !studio_workspace_source.contains(QStringLiteral("beginActiveLuminanceRangeSample")) ||
      !studio_workspace_source.contains(QStringLiteral("commitActiveLuminanceRangeSample"))) {
    return fail("luminance range mask UI should expose drag patch sampling and remove controls");
  }
  if (!mask_inspector_source.contains(QStringLiteral("activeMaskDepthRangeSampleButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskDepthRangeSampleCountLabel")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskDepthRangeRemoveSampleButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskDepthSamplingDisabledReason")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskDepthSamplingAvailable")) ||
      !studio_workspace_source.contains(QStringLiteral("maskDepthRangeSampling")) ||
      !studio_workspace_source.contains(QStringLiteral("sampleActiveDepthRangeAt"))) {
    return fail("depth range mask UI should expose depth-gated sampling and disabled reason copy");
  }
  if (!mask_inspector_source.contains(QStringLiteral("generatedMaskCleanupSummary")) ||
      !mask_inspector_source.contains(QStringLiteral("generatedMaskHoleFillThresholdSlider")) ||
      !mask_inspector_source.contains(QStringLiteral("activeGeneratedMaskHoleFillThreshold")) ||
      !mask_inspector_source.contains(QStringLiteral("generatedMaskIslandRemovalThresholdSlider")) ||
      !mask_inspector_source.contains(QStringLiteral("activeGeneratedMaskIslandRemovalThreshold"))) {
    return fail("generated mask UI should expose hole-fill and island-removal cleanup thresholds");
  }
  if (!mask_inspector_source.contains(QStringLiteral("maskEdgeRefinePreview")) ||
      !mask_inspector_source.contains(QStringLiteral("maskEdgePreviewShiftMarker")) ||
      !mask_inspector_source.contains(QStringLiteral("maskEdgePreviewContrastBand")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskShiftEdgeSlider")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskContrastEdgeSlider")) ||
      !mask_inspector_source.contains(QStringLiteral("maskInspectorShowOriginalRefinementButton")) ||
      !mask_inspector_source.contains(QStringLiteral("maskInspectorShowRefinedRefinementButton")) ||
      !mask_inspector_source.contains(QStringLiteral("maskInspectorHighQualityRefinementPreviewButton")) ||
      !mask_inspector_source.contains(QStringLiteral("maskInspectorFullResolutionPreviewButton")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewRefinementEdgeButton")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskPreviewRefinementEdgeButton\"")) ||
      !mask_inspector_source.contains(QStringLiteral("visible: false")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskRefinementPreviewMode = \"original\"")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskRefinementPreviewMode = \"refined\"")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskRefinementHighQualityPreview")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskFullResolutionPreview")) ||
      !mask_inspector_source.contains(QStringLiteral("function maskPreviewQualityLabel")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.activeMaskPreviewQualityTier")) ||
      !studio_workspace_source.contains(QStringLiteral("function activeMaskPreviewQualityLabel()")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.activeMaskPreviewQualityTier")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskEdgeSection\"")) ||
      !mask_inspector_source.contains(QStringLiteral("advanced refine changes active")) ||
      !mask_inspector_source.contains(QStringLiteral("Advanced refinement is collapsed until needed.")) ||
      mask_inspector_source.contains(QStringLiteral("expanded: editRoot.isAiMaskKind(editorPreview.activeMaskKind)")) ||
      !mask_inspector_source.contains(QStringLiteral("label: \"Shift Edge\"")) ||
      !mask_inspector_source.contains(QStringLiteral("label: \"Contrast Edge\"")) ||
      !mask_view_source.contains(QStringLiteral("function activeRefineEffectCount()")) ||
      !mask_view_source.contains(QStringLiteral("Fine Tune · ")) ||
      !mask_view_source.contains(QStringLiteral("advanced refine changes active")) ||
      !mask_view_source.contains(QStringLiteral("Advanced refinement is collapsed until needed.")) ||
      !mask_view_source.contains(QStringLiteral("maskViewShowOriginalRefinementButton")) ||
      !mask_view_source.contains(QStringLiteral("maskViewShowRefinedRefinementButton")) ||
      !mask_view_source.contains(QStringLiteral("maskViewHighQualityRefinementPreviewButton")) ||
      !mask_view_source.contains(QStringLiteral("maskViewRefinementEdgePreviewButton")) ||
      mask_view_source.contains(QStringLiteral("editorPreview.maskPreviewMode = \"refinement-edge\"")) ||
      !mask_view_source.contains(QStringLiteral("editorPreview.maskRefinementPreviewMode = \"original\"")) ||
      !mask_view_source.contains(QStringLiteral("editorPreview.maskRefinementPreviewMode = \"refined\"")) ||
      !mask_view_source.contains(QStringLiteral("editorPreview.maskRefinementHighQualityPreview")) ||
      !mask_view_source.contains(QStringLiteral("label: \"Shift Edge\"")) ||
      !mask_view_source.contains(QStringLiteral("label: \"Contrast Edge\"")) ||
      mask_view_source.contains(QStringLiteral("label: \"Expand / Contract\""))) {
    return fail("selection refinement UI should expose separate Shift Edge and Contrast Edge controls, original/refined preview, and a visual edge preview");
  }
  if (!mask_inspector_source.contains(QStringLiteral("label: \"Pressure Size\"")) ||
      !mask_inspector_source.contains(QStringLiteral("label: \"Pressure Flow\"")) ||
      !mask_inspector_source.contains(QStringLiteral("label: \"Pressure Opacity\"")) ||
      !mask_view_source.contains(QStringLiteral("label: \"Pressure Size\"")) ||
      !mask_view_source.contains(QStringLiteral("label: \"Pressure Flow\"")) ||
      !mask_view_source.contains(QStringLiteral("label: \"Pressure Opacity\""))) {
    return fail("brush mask UI should expose tablet pressure response sliders for size, flow, and opacity");
  }
  if (!mask_inspector_source.contains(QStringLiteral("Sample First Dab")) ||
      !mask_inspector_source.contains(QStringLiteral("Sample Each Dab")) ||
      !mask_view_source.contains(QStringLiteral("Sample First Dab")) ||
      !mask_view_source.contains(QStringLiteral("Sample Each Dab"))) {
    return fail("brush auto-mask UI should expose first-dab and continuous center sampling modes");
  }
  if (!mask_inspector_source.contains(QStringLiteral("maskSourceDragHandler")) ||
      !mask_inspector_source.contains(QStringLiteral("maskSourceReorderDropArea")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.moveMaskSource")) ||
      !mask_view_source.contains(QStringLiteral("maskSourceDragHandler")) ||
      !mask_view_source.contains(QStringLiteral("maskSourceReorderDropArea")) ||
      !mask_view_source.contains(QStringLiteral("editorPreview.moveMaskSource"))) {
    return fail("selection source lists should expose drag-to-reorder and cross-set move affordances");
  }
  if (!mask_view_source.contains(QStringLiteral("beginAlphaImageMaskImport")) ||
      !mask_view_source.contains(QStringLiteral("alphaMaskImportPreviewPanel")) ||
      !mask_view_source.contains(QStringLiteral("alphaMaskImportInvertCheck")) ||
      !mask_view_source.contains(QStringLiteral("alphaMaskImportThresholdSlider")) ||
      !mask_view_source.contains(QStringLiteral("alphaMaskImportResampleModeCombo")) ||
      !mask_view_source.contains(QStringLiteral("alphaMaskImportCommitButton")) ||
      !mask_view_source.contains(QStringLiteral("commitAlphaImageMaskImport"))) {
    return fail("alpha image mask UI should preview invert, threshold, and resample settings before commit");
  }
  if (!mask_view_source.contains(QStringLiteral("Sky (Heuristic)")) ||
      !mask_view_source.contains(QStringLiteral("Background (Subject Invert)")) ||
      !mask_view_source.contains(QStringLiteral("People (Unsupported)")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedPeopleUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedFaceUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedHairUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedClothingUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedFacialHairUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedLipsUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedTeethUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedEyesUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedSkinUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedWaterUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedVegetationUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedMountainsUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedGroundUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedArchitectureUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedSnowUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("visible: false")) ||
      !mask_view_source.contains(QStringLiteral("not semantic sky segmentation")) ||
      !mask_view_source.contains(QStringLiteral("not a separate background model")) ||
      !mask_view_source.contains(QStringLiteral("person-part matte")) ||
      !mask_view_source.contains(QStringLiteral("real landscape semantic matte"))) {
    return fail("generated mask tools should label heuristic/derived paths and hide unsupported semantic masks");
  }
  if (!mask_view_source.contains(QStringLiteral("editorPreview.beginSmartSelectStroke(")) ||
      !mask_view_source.contains(QStringLiteral("editorPreview.updateSmartSelectStroke(")) ||
      !mask_view_source.contains(QStringLiteral("editorPreview.commitSmartSelectStroke(objectPickMode, objectPickCreateNew)")) ||
      !mask_view_source.contains(QStringLiteral("editorPreview.smartSelectStrokeActive")) ||
      mask_view_source.contains(QStringLiteral("property var smartSelectStrokePoints")) ||
      mask_view_source.contains(QStringLiteral("function smartSelectStrokePromptPayload")) ||
      mask_view_source.contains(QStringLiteral("scheduleSmartSelectPreviewPath(smartSelectStrokePromptPayload())")) ||
      mask_view_source.contains(QStringLiteral("pickMaskPath(smartSelectStrokePromptPayload()"))) {
    return fail("object-drag smart selection strokes should use controller-backed stroke state instead of QML-owned prompt payloads");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewAcceptButton")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewRejectButton")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("objectName: \"smartSelectPreviewCommitControls\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("previewCommitContract: \"preview-only|accept-reject|required-before-layer:v1\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewPreviousButton")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewNextButton")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectCandidateHudLabel")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectCandidateHudText")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("acceptSmartSelectPreview")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("rejectSmartSelectPreview"))) {
    return fail("smart selection candidate previews should expose cycle, accept, and reject controls");
  }
  if (!studio_workspace_source.contains(QStringLiteral("root.objectPickArmed ||")) ||
      !studio_workspace_source.contains(QStringLiteral("property bool objectPickArmed: false")) ||
      !studio_workspace_source.contains(QStringLiteral("property string objectPickMode: \"selection\"")) ||
      !studio_workspace_source.contains(QStringLiteral("return \"Object\"")) ||
      !studio_workspace_source.contains(QStringLiteral("\"Hover or drag / \" + root.maskOperationLabel(root.objectPickMode)")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.acceptSmartSelectPreview(root.objectPickMode, root.objectPickCreateNew)")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.rejectSmartSelectPreview()"))) {
    return fail("object selection should use the shared active tool chip with Done and Cancel routes");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewOutline")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("candidateOutlineRenderContract: \"smart-selection-hover|candidate-outline|precommit:v1\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("candidatePreviewPipelineSignature: editorPreview.smartSelectPreviewPipelineSignature")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartPreviewRasterContract: \"disabled|outline-only-candidate|no-speckled-matte:v1\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("id: smartPreviewMatte")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("visible: false")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("candidateOutlineVisible: editRoot.objectPickArmed")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("visible: candidateOutlineVisible"))) {
    return fail("smart selection hover should draw only a clean pre-commit candidate outline");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("smartSelectHoverPrewarmContract: \"pointer-pause|jitter-gated|no-enter-prewarm:v1\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewPauseMs: 180")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function smartSelectPreviewPointNear(")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewPauseTimer.restart()")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editorPreview.scheduleSmartSelectPreview(root.pendingSmartSelectPreviewX")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("root.lastSmartSelectPreviewMs"))) {
    return fail("smart selection hover previews should wait for pointer pause before they reach the renderer");
  }
  if (!editor_preview_controller_source.contains(QStringLiteral("pending_interactive_preempts_full")) ||
      !editor_preview_controller_source.contains(QStringLiteral("preview.render.interactive_preempted_full")) ||
      !editor_preview_controller_source.contains(QStringLiteral("pending_interactive_request_.render_priority < pending_full_request_.render_priority")) ||
      !editor_preview_controller_source.contains(QStringLiteral("startRenderStage(false, overlay_request)"))) {
    return fail("visible overlay renders should preempt pending background full-resolution renders");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("handleSmartSelectPromptModifier")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("Qt.ShiftModifier")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("Qt.AltModifier")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("refineSmartSelectPreviewWithPrompt")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectModifierPromptHandled")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editorPreview.beginSmartSelectStroke(point.x, point.y, previewItem.width, previewItem.height)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editorPreview.updateSmartSelectStroke(point.x, point.y, previewItem.width, previewItem.height)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editorPreview.commitSmartSelectStroke(")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("property var smartSelectStrokePoints")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("function smartSelectStrokePromptPayload")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("scheduleSmartSelectPreviewPath(smartSelectStrokePromptPayload())"))) {
    return fail("smart selection overlay should support Shift positive prompts, Option negative prompts, and controller-backed drag prompts");
  }
  QFile mask_glossary_doc(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)).filePath(QStringLiteral("docs/masking-glossary.md")));
  if (!mask_glossary_doc.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect the masking glossary");
  }
  const auto mask_glossary_source = QString::fromUtf8(mask_glossary_doc.readAll());
  const QStringList required_mask_glossary_terms = {
      QStringLiteral("| Selection |"),
      QStringLiteral("| Retouch |"),
      QStringLiteral("| Retouch patch |"),
      QStringLiteral("| Mask set |"),
      QStringLiteral("| Source |"),
      QStringLiteral("| Operation |"),
      QStringLiteral("| Overlay |"),
      QStringLiteral("| Generated mask |"),
      QStringLiteral("| Heuristic mask |"),
      QStringLiteral("| Stale generated mask |"),
  };
  for (const auto& term : required_mask_glossary_terms) {
    if (!mask_glossary_source.contains(term)) {
      return fail("masking glossary should define the shared Selection/Retouch vocabulary");
    }
  }
  QFile generated_mask_corpus_file(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("tests/fixtures/generated_mask_benchmark_corpus.json")));
  if (!generated_mask_corpus_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect the generated-mask benchmark corpus");
  }
  const auto generated_mask_corpus_document =
      QJsonDocument::fromJson(generated_mask_corpus_file.readAll());
  if (!generated_mask_corpus_document.isObject()) {
    return fail("generated-mask benchmark corpus should be a JSON object");
  }
  const auto generated_mask_corpus = generated_mask_corpus_document.object();
  if (generated_mask_corpus.value(QStringLiteral("schemaVersion")).toInt() < 1 ||
      generated_mask_corpus.value(QStringLiteral("name")).toString() !=
          QStringLiteral("generated-mask-benchmark-synthetic-v1")) {
    return fail("generated-mask benchmark corpus should expose a stable schema and name");
  }
  const QSet<QString> required_generated_mask_categories = {
      QStringLiteral("portrait"),
      QStringLiteral("product"),
      QStringLiteral("landscape"),
      QStringLiteral("low_contrast"),
      QStringLiteral("busy_background"),
      QStringLiteral("transparent_object"),
  };
  QSet<QString> manifest_required_categories;
  for (const auto& value : generated_mask_corpus.value(QStringLiteral("requiredCategories")).toArray()) {
    manifest_required_categories.insert(value.toString());
  }
  if (manifest_required_categories != required_generated_mask_categories) {
    return fail("generated-mask benchmark corpus should declare every required benchmark category");
  }
  QSet<QString> generated_mask_case_categories;
  const auto generated_mask_cases = generated_mask_corpus.value(QStringLiteral("cases")).toArray();
  if (generated_mask_cases.size() < required_generated_mask_categories.size()) {
    return fail("generated-mask benchmark corpus should include at least one case per required category");
  }
  for (const auto& value : generated_mask_cases) {
    const auto test_case = value.toObject();
    const auto category = test_case.value(QStringLiteral("category")).toString();
    generated_mask_case_categories.insert(category);
    const auto quality_checks = test_case.value(QStringLiteral("qualityChecks")).toObject();
    if (test_case.value(QStringLiteral("id")).toString().trimmed().isEmpty() ||
        !required_generated_mask_categories.contains(category) ||
        test_case.value(QStringLiteral("source")).toString() != QStringLiteral("synthetic") ||
        test_case.value(QStringLiteral("width")).toInt() < 384 ||
        test_case.value(QStringLiteral("height")).toInt() < 384 ||
        test_case.value(QStringLiteral("generatedKind")).toString().trimmed().isEmpty() ||
        test_case.value(QStringLiteral("promptType")).toString().trimmed().isEmpty() ||
        test_case.value(QStringLiteral("stressors")).toArray().isEmpty() ||
        test_case.value(QStringLiteral("expectedFeatures")).toArray().isEmpty() ||
        quality_checks.value(QStringLiteral("minSelectedFraction")).toDouble() <= 0.0 ||
        quality_checks.value(QStringLiteral("maxSelectedFraction")).toDouble() <=
            quality_checks.value(QStringLiteral("minSelectedFraction")).toDouble() ||
        !quality_checks.value(QStringLiteral("requiresBoundaryPixels")).toBool() ||
        !quality_checks.value(QStringLiteral("requiresInteriorPixels")).toBool() ||
        !quality_checks.value(QStringLiteral("requiresExteriorPixels")).toBool()) {
      return fail("generated-mask benchmark corpus cases should be complete and quality-checkable");
    }
  }
  if (generated_mask_case_categories != required_generated_mask_categories) {
    return fail("generated-mask benchmark corpus should cover portrait, product, landscape, low-contrast, busy-background, and transparent-object cases");
  }
  const QStringList forbidden_selection_copy = {
      QStringLiteral("Paint a manual mask source."),
      QStringLiteral("Create a linear gradient mask source."),
      QStringLiteral("Create a radial mask source."),
      QStringLiteral("Retry the last failed mask action."),
      QStringLiteral("Paint into the active mask."),
      QStringLiteral("Erase from the active mask."),
      QStringLiteral("Layers / Masks"),
      QStringLiteral("Base / Masks"),
      QStringLiteral("Mask Exposure"),
      QStringLiteral("Mask Temp"),
      QStringLiteral("Mask Stack"),
      QStringLiteral("Mask Sets"),
      QStringLiteral("Mask Properties"),
      QStringLiteral("Mask set name"),
      QStringLiteral("No mask sets yet"),
      QStringLiteral("No selection sets yet"),
      QStringLiteral("Selection Stack"),
      QStringLiteral("Selection Sets"),
      QStringLiteral("Exit Mask"),
      QStringLiteral("Auto Mask"),
      QStringLiteral("Freehand Mask"),
      QStringLiteral("Auto-Mask Range"),
  };
  const auto combined_selection_ui_source =
      studio_workspace_source + QLatin1Char('\n') + mask_view_source + QLatin1Char('\n') +
      mask_inspector_source;
  for (const auto& phrase : forbidden_selection_copy) {
    if (combined_selection_ui_source.contains(phrase)) {
      return fail("selection and retouch UI should not use legacy mask vocabulary for local workflows");
    }
  }
  const auto mask_view_active_controls_index =
      mask_view_source.indexOf(QStringLiteral("objectName: \"activeMaskControlsSection\""));
  const auto mask_view_adjustment_index =
      mask_view_source.indexOf(QStringLiteral("objectName: \"activeMaskAdjustmentSection\""));
  const auto mask_inspector_active_controls_index =
      mask_inspector_source.indexOf(QStringLiteral("objectName: \"activeMaskControlsSection\""));
  const auto mask_inspector_adjustment_index =
      mask_inspector_source.indexOf(QStringLiteral("objectName: \"activeMaskAdjustmentSection\""));
  if (mask_view_active_controls_index < 0 || mask_view_adjustment_index < 0 ||
      mask_inspector_active_controls_index < 0 || mask_inspector_adjustment_index < 0 ||
      mask_view_active_controls_index > mask_view_adjustment_index ||
      mask_inspector_active_controls_index > mask_inspector_adjustment_index) {
    return fail("mask active controls should stay above local adjustment sliders");
  }
  if (!studio_workspace_source.contains(QStringLiteral("Paint an additive brush selection.")) ||
      !studio_workspace_source.contains(QStringLiteral("Paint a subtractive brush selection.")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskShortcutContextActive(requireActiveMask)")) ||
      !studio_workspace_source.contains(QStringLiteral("function createMaskFromShortcut()")) ||
      !studio_workspace_source.contains(QStringLiteral("function selectMaskMoveFromShortcut()")) ||
      !studio_workspace_source.contains(QStringLiteral("function startMaskLassoFromShortcut()")) ||
      !studio_workspace_source.contains(QStringLiteral("function startMaskPolygonFromShortcut()")) ||
      !studio_workspace_source.contains(QStringLiteral("function setMaskOperationFromShortcut(operationName)")) ||
      !studio_workspace_source.contains(QStringLiteral("function shortcutTextInputActive()")) ||
      !studio_workspace_source.contains(QStringLiteral("function brushShortcutContextActive()")) ||
      !studio_workspace_source.contains(QStringLiteral("function invertActiveMaskFromShortcut()")) ||
      !studio_workspace_source.contains(QStringLiteral("function setTemporaryBrushEraseFromShortcut(active)")) ||
      !studio_workspace_source.contains(QStringLiteral("function toggleActiveMaskOverlayFromShortcut()")) ||
      !studio_workspace_source.contains(QStringLiteral("function toggleMaskOutlineFromShortcut()")) ||
      !studio_workspace_source.contains(QStringLiteral("function switchLayerTargetFromShortcut(delta)")) ||
      !studio_workspace_source.contains(QStringLiteral("function switchMaskTargetFromShortcut(delta)")) ||
      !studio_workspace_source.contains(QStringLiteral("function returnToBaseImageFromShortcut()")) ||
      !studio_workspace_source.contains(
          QStringLiteral("function setMaskPreviewTemporarilyHiddenFromShortcut(hidden)")) ||
      !studio_workspace_source.contains(QStringLiteral("function deleteActiveMaskFromShortcut()")) ||
      !studio_workspace_source.contains(QStringLiteral("function renameActiveMaskFromShortcut()")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskShortcutSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskCreateShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskOperationAddShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskOperationSubtractShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskOperationIntersectShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskInvertShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskOverlayShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskOutlineShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"layerTargetNextShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"layerTargetPreviousShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskTargetNextShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskTargetPreviousShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"baseImageTargetShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskSelectMoveShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskLassoShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskPolygonShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskSourcePolygonButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("Keys.onPressed: function(event)")) ||
      !studio_workspace_source.contains(QStringLiteral("Keys.onReleased: function(event)")) ||
      !studio_workspace_source.contains(QStringLiteral("Qt.Key_H")) ||
      !studio_workspace_source.contains(QStringLiteral("Qt.Key_X")) ||
      !studio_workspace_source.contains(QStringLiteral("temporaryBrushEraseActive")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.maskPreviewTemporarilyHidden")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskDeleteShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskRenameShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeMaskRenameDialog\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeMaskRenameShortcutField\"")) ||
      !studio_workspace_source.contains(QStringLiteral("select:V|brush:B|erase:E|lasso:L|polygon:P|")) ||
      !studio_workspace_source.contains(QStringLiteral("create:Ctrl+Shift+M|add:Ctrl+Shift+A|subtract:Ctrl+Shift+S|")) ||
      !studio_workspace_source.contains(QStringLiteral("outline:Ctrl+Shift+U|layer-target:Ctrl+Shift+L|layer-target-prev:Ctrl+Alt+Shift+L|")) ||
      !studio_workspace_source.contains(QStringLiteral("mask-target:Ctrl+Shift+K|mask-target-prev:Ctrl+Alt+Shift+K|")) ||
      !studio_workspace_source.contains(QStringLiteral("base-image:Ctrl+Shift+B|temp-erase:X-hold|temporary-hide:H-hold|")) ||
      !studio_workspace_source.contains(QStringLiteral("Layers / Selections")) ||
      !mask_view_source.contains(QStringLiteral("title: \"Masks\"")) ||
      !mask_view_source.contains(QStringLiteral("Build each mask from ordered Components")) ||
      !mask_view_source.contains(QStringLiteral("createNewMaskButton")) ||
      !mask_view_source.contains(QStringLiteral("text: \"Create New Mask\"")) ||
      !mask_view_source.contains(QStringLiteral("createNewMaskChooser")) ||
      !mask_view_source.contains(QStringLiteral("createMaskSmartButton")) ||
      !mask_view_source.contains(QStringLiteral("createMaskBrushAddButton")) ||
      !mask_view_source.contains(QStringLiteral("createMaskPolygonButton")) ||
      !mask_view_source.contains(QStringLiteral("createMaskAlphaImageButton")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskEmptyStateGuidance\"")) ||
      !mask_view_source.contains(QStringLiteral("visible: editorPreview.maskSetCount === 0")) ||
      !mask_view_source.contains(QStringLiteral("No masks yet. Create a mask with Smart Select")) ||
      !mask_view_source.contains(QStringLiteral("maskSetThumbnail")) ||
      !mask_view_source.contains(QStringLiteral("maskSourceThumbnail")) ||
      !mask_view_source.contains(QStringLiteral("modelData.thumbnailSource")) ||
      !mask_view_source.contains(QStringLiteral("maskComponentsHeaderLabel")) ||
      !mask_view_source.contains(QStringLiteral("text: \"Components\"")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskOperationAddButton")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskOperationSubtractButton")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskOperationIntersectButton")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskOperationReplaceButton")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskInvertButton")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskHideButton")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskLockButton")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskRenameField")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskDuplicateButton")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskDeleteButton")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskControlsSection\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskAdjustmentSection\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustToneGroup\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustColorGroup\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustPresenceGroup\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustDetailGroup\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustEffectsGroup\"")) ||
      !mask_view_source.contains(QStringLiteral("maskCompareAdjustmentGroups")) ||
      !mask_view_source.contains(QStringLiteral("toggleMaskCompareGroup")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustToneBeforeAfterButton\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustColorBeforeAfterButton\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustPresenceBeforeAfterButton\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustDetailBeforeAfterButton\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustEffectsBeforeAfterButton\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSearchField\"")) ||
      !mask_view_source.contains(QStringLiteral("placeholderText: \"Search masks, tools, controls\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSearchResultsPanel\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSearchResultButton\"")) ||
      !mask_view_source.contains(QStringLiteral("maskSearchToolItems")) ||
      !mask_view_source.contains(QStringLiteral("maskSearchAdjustmentItems")) ||
      !mask_view_source.contains(QStringLiteral("activateMaskSearchResult")) ||
      !mask_view_source.contains(QStringLiteral("filter(root.maskMatchesSearch)")) ||
      !mask_view_source.contains(QStringLiteral("function activeMaskEditingContext()")) ||
      !mask_view_source.contains(QStringLiteral("function activeMaskEditingContextSignature()")) ||
      !mask_view_source.contains(QStringLiteral("function maskContextVisible(contextName)")) ||
      !mask_view_source.contains(QStringLiteral("function openCreateMaskContext()")) ||
      !mask_view_source.contains(QStringLiteral("function activeMaskCompactHeaderSignature()")) ||
      !mask_view_source.contains(QStringLiteral("function activeMaskHeaderTarget()")) ||
      !mask_view_source.contains(QStringLiteral("function renameMaskRow(flatIndex)")) ||
      !mask_view_source.contains(QStringLiteral("function toggleMaskRowEnabled(flatIndex, enabled)")) ||
      !mask_view_source.contains(QStringLiteral("function toggleMaskRowSolo(flatIndex, solo)")) ||
      !mask_view_source.contains(QStringLiteral("function toggleMaskRowLocked(flatIndex, locked)")) ||
      !mask_view_source.contains(QStringLiteral("function moveMaskRow(flatIndex, offset)")) ||
      !mask_view_source.contains(QStringLiteral("function duplicateMaskRow(flatIndex)")) ||
      !mask_view_source.contains(QStringLiteral("function deleteMaskRow(flatIndex)")) ||
      !mask_view_source.contains(QStringLiteral("function applyRetouchRow(flatIndex)")) ||
      !mask_view_source.contains(QStringLiteral("function cancelRetouchRow(flatIndex)")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskActiveContextHeader\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskActiveContextLabel\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskActiveContextSignature\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextMenu\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextSelectItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextRenameItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextEnableItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextSoloItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextLockItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextMoveUpItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextMoveDownItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextDuplicateItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextDeleteItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"retouchRowContextMenu\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"retouchRowContextApplyItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"retouchRowContextCancelItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSetFocusRow\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceFocusRow\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"retouchFocusRow\"")) ||
      !mask_view_source.contains(QStringLiteral("activeFocusOnTab: true")) ||
      !mask_view_source.contains(QStringLiteral("Keys.onReturnPressed: editorPreview.activeMaskIndex = modelData.flatIndex")) ||
      !mask_view_source.contains(QStringLiteral("Keys.onSpacePressed: editorPreview.activeMaskIndex = modelData.flatIndex")) ||
      !mask_view_source.contains(QStringLiteral("border.color: activeFocus")) ||
      !mask_view_source.contains(QStringLiteral("acceptedButtons: Qt.LeftButton | Qt.RightButton")) ||
      !mask_view_source.contains(QStringLiteral("maskSourceContextMenu.popup()")) ||
      !mask_view_source.contains(QStringLiteral("retouchRowContextMenu.popup()")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskCompactHeader\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskHeaderNameLabel\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskHeaderKindLabel\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskHeaderOperationLabel\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskHeaderTargetLabel\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskHeaderOpacityLabel\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskHeaderEnabledButton\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskHeaderSoloButton\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskHeaderLockButton\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskHeaderSignature\"")) ||
      !mask_view_source.contains(QStringLiteral("|target:\" + root.activeMaskHeaderTarget()")) ||
      !mask_view_source.contains(QStringLiteral("|solo:\" + editorPreview.activeMaskSolo.toString()")) ||
      !mask_view_source.contains(QStringLiteral("|opacity:\" + root.percentText(editorPreview.activeMaskOpacity)")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskDensityOpacityHelp\"")) ||
      !mask_view_source.contains(QStringLiteral("Density sets maximum mask strength. Opacity controls blend amount.")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskOpacitySlider\"")) ||
      !mask_view_source.contains(QStringLiteral("label: \"Mask Opacity\"")) ||
      !mask_view_source.contains(QStringLiteral("valueText: root.percentText(editorPreview.activeMaskOpacity)")) ||
      !mask_view_source.contains(QStringLiteral("onValueDragged: function(value) { editorPreview.activeMaskOpacity = value / 100 }")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskDensitySlider\"")) ||
      !mask_view_source.contains(QStringLiteral("label: \"Mask Density\"")) ||
      !mask_view_source.contains(QStringLiteral("valueText: root.percentText(editorPreview.activeMaskDensity)")) ||
      !mask_view_source.contains(QStringLiteral("onValueDragged: function(value) { editorPreview.activeMaskDensity = value / 100 }")) ||
      !mask_view_source.contains(QStringLiteral("readonly property int stableInspectorWidth: 410")) ||
      !mask_view_source.contains(QStringLiteral("readonly property int minimumCanvasWidth: 360")) ||
      !mask_view_source.contains(QStringLiteral("readonly property int minimumCanvasHeight: 320")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskWorkspaceStableSplit\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskCanvasPane\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskCanvasViewport\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskViewPointerHandlerLayer\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskViewToolHoverHandler\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskViewSmartSelectPointHandler\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskViewLassoPointHandler\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskViewPolygonTapHandler\"")) ||
      !mask_view_source.contains(QStringLiteral("onDoubleTapped:")) ||
      !mask_view_source.contains(QStringLiteral("HoverHandler {")) ||
      !mask_view_source.contains(QStringLiteral("PointHandler {")) ||
      !mask_view_source.contains(QStringLiteral("TapHandler {")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskInspectorPane\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskInspectorScrollView\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskInspectorColumn\"")) ||
      !mask_view_source.contains(QStringLiteral("Layout.minimumWidth: root.minimumCanvasWidth")) ||
      !mask_view_source.contains(QStringLiteral("Layout.preferredWidth: root.stableInspectorWidth")) ||
      !mask_view_source.contains(QStringLiteral("Layout.maximumWidth: root.stableInspectorWidth")) ||
      !mask_view_source.contains(QStringLiteral("contentWidth: availableWidth")) ||
      !mask_view_source.contains(QStringLiteral("contentHeight: maskInspectorColumn.implicitHeight")) ||
      !mask_view_source.contains(QStringLiteral("root.objectPickArmed || editorPreview.smartSelectStrokeActive || editorPreview.smartSelectPreviewActive")) ||
      !mask_view_source.contains(QStringLiteral("|smart-select:\" + (context === \"smart-select\").toString()")) ||
      !mask_view_source.contains(QStringLiteral("visible: root.maskContextVisible(\"create\")")) ||
      !mask_view_source.contains(QStringLiteral("visible: root.maskContextVisible(\"brush\")")) ||
      !mask_view_source.contains(QStringLiteral("visible: root.maskContextVisible(\"polygon\")")) ||
      !mask_view_source.contains(QStringLiteral("visible: root.maskContextVisible(\"refine\") && root.activeMaskSupportsSelectionRefine()")) ||
      !mask_view_source.contains(QStringLiteral("visible: root.maskContextVisible(\"refine\") && editorPreview.hasActiveMask")) ||
      !mask_view_source.contains(QStringLiteral("visible: root.maskActiveControlsContextVisible()")) ||
      !mask_view_source.contains(QStringLiteral("Active Selection")) ||
      !mask_inspector_source.contains(QStringLiteral("title: \"Masks\"")) ||
      !mask_inspector_source.contains(QStringLiteral("Each mask contains ordered Components")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorEmptyStateGuidance\"")) ||
      !mask_inspector_source.contains(QStringLiteral("visible: editorPreview.maskSetCount === 0")) ||
      !mask_inspector_source.contains(QStringLiteral("text: \"No masks yet\"")) ||
      !mask_inspector_source.contains(QStringLiteral("maskSetThumbnail")) ||
      !mask_inspector_source.contains(QStringLiteral("maskSourceThumbnail")) ||
      !mask_inspector_source.contains(QStringLiteral("modelData.thumbnailSource")) ||
      !mask_inspector_source.contains(QStringLiteral("maskComponentsHeaderLabel")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskOperationAddButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskOperationSubtractButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskOperationIntersectButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskOperationReplaceButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskInvertButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskHideButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskLockButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskRenameField")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskDuplicateButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskDeleteButton")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"activeMaskControlsSection\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"activeMaskAdjustmentSection\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustToneGroup\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustColorGroup\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustPresenceGroup\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustDetailGroup\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustEffectsGroup\"")) ||
      !mask_inspector_source.contains(QStringLiteral("maskCompareAdjustmentGroups")) ||
      !mask_inspector_source.contains(QStringLiteral("toggleMaskCompareGroup")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustToneBeforeAfterButton\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustColorBeforeAfterButton\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustPresenceBeforeAfterButton\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustDetailBeforeAfterButton\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustEffectsBeforeAfterButton\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskSearchField\"")) ||
      !mask_inspector_source.contains(QStringLiteral("placeholderText: \"Search masks, tools, controls\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskSearchResultsPanel\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskSearchResultButton\"")) ||
      !mask_inspector_source.contains(QStringLiteral("maskSearchToolItems")) ||
      !mask_inspector_source.contains(QStringLiteral("maskSearchAdjustmentItems")) ||
      !mask_inspector_source.contains(QStringLiteral("activateMaskSearchResult")) ||
      !mask_inspector_source.contains(QStringLiteral("filter(root.maskMatchesSearch)")) ||
      !mask_inspector_source.contains(QStringLiteral("function renameMaskRow(flatIndex)")) ||
      !mask_inspector_source.contains(QStringLiteral("function toggleMaskRowEnabled(flatIndex, enabled)")) ||
      !mask_inspector_source.contains(QStringLiteral("function toggleMaskRowSolo(flatIndex, solo)")) ||
      !mask_inspector_source.contains(QStringLiteral("function toggleMaskRowLocked(flatIndex, locked)")) ||
      !mask_inspector_source.contains(QStringLiteral("function moveMaskRow(flatIndex, offset)")) ||
      !mask_inspector_source.contains(QStringLiteral("function duplicateMaskRow(flatIndex)")) ||
      !mask_inspector_source.contains(QStringLiteral("function deleteMaskRow(flatIndex)")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextMenu\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextSelectItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextRenameItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextEnableItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextSoloItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextLockItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextMoveUpItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextMoveDownItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextDuplicateItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextDeleteItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorRetouchContextMenu\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorRetouchContextApplyItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorRetouchContextCancelItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSetFocusRow\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceFocusRow\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorRetouchFocusRow\"")) ||
      !mask_inspector_source.contains(QStringLiteral("activeFocusOnTab: true")) ||
      !mask_inspector_source.contains(QStringLiteral("Keys.onReturnPressed: editorPreview.activeMaskIndex = modelData.flatIndex")) ||
      !mask_inspector_source.contains(QStringLiteral("Keys.onSpacePressed: editorPreview.activeMaskIndex = modelData.flatIndex")) ||
      !mask_inspector_source.contains(QStringLiteral("border.color: activeFocus")) ||
      !mask_inspector_source.contains(QStringLiteral("maskInspectorSourceContextMenu.popup()")) ||
      !mask_inspector_source.contains(QStringLiteral("maskInspectorRetouchContextMenu.popup()")) ||
       !mask_inspector_source.contains(QStringLiteral("label: \"Remove\"")) ||
       !mask_inspector_source.contains(QStringLiteral("Selection Properties")) ||
       !mask_inspector_source.contains(QStringLiteral("Edge-Aware Range"))) {
    return fail("selection and retouch UI should expose the revised selection vocabulary");
  }
  const auto studio_shortcuts = literal_shortcut_sequences(studio_workspace_source);
  const auto legacy_edit_shortcuts = literal_shortcut_sequences(edit_view_source);
  const QSet<QString> intentional_legacy_mask_shortcut_overlaps = {
      QStringLiteral("Return"),
      QStringLiteral("Enter"),
      QStringLiteral("Backspace"),
      QStringLiteral("Delete"),
      QStringLiteral("B"),
      QStringLiteral("E"),
      QStringLiteral("V"),
      QStringLiteral("["),
      QStringLiteral("]"),
  };
  QStringList unexpected_legacy_mask_shortcut_overlaps;
  for (const auto& sequence : studio_shortcuts) {
    if (legacy_edit_shortcuts.contains(sequence) &&
        !intentional_legacy_mask_shortcut_overlaps.contains(sequence)) {
      unexpected_legacy_mask_shortcut_overlaps.append(sequence);
    }
  }
  unexpected_legacy_mask_shortcut_overlaps.sort();
  if (!unexpected_legacy_mask_shortcut_overlaps.isEmpty() ||
      !studio_shortcuts.contains(QStringLiteral("Ctrl+Shift+L")) ||
      !studio_shortcuts.contains(QStringLiteral("Ctrl+Alt+Shift+L")) ||
      !studio_shortcuts.contains(QStringLiteral("Ctrl+Shift+K")) ||
      !studio_shortcuts.contains(QStringLiteral("Ctrl+Alt+Shift+K")) ||
      !studio_shortcuts.contains(QStringLiteral("Ctrl+Shift+B")) ||
      !intentional_legacy_mask_shortcut_overlaps.contains(QStringLiteral("B")) ||
      !intentional_legacy_mask_shortcut_overlaps.contains(QStringLiteral("["))) {
    std::cerr << "Unexpected Studio/EditView shortcut overlaps: "
              << unexpected_legacy_mask_shortcut_overlaps.join(QStringLiteral(", ")).toStdString()
              << '\n';
    return fail("Studio mask and brush shortcuts should not conflict with legacy EditView shortcuts outside the explicit mask-tool allowlist");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function defaultCurvePoints()")) ||
      !studio_workspace_source.contains(QStringLiteral("function primaryCurveMode()")) ||
      !studio_workspace_source.contains(QStringLiteral("function primaryCurvePoints()")) ||
      !studio_workspace_source.contains(QStringLiteral("const layerIndex = root.activeAdjustmentLayerIndex()")) ||
      !studio_workspace_source.contains(QStringLiteral("if (layerIndex < 0)")) ||
      !studio_workspace_source.contains(QStringLiteral("return editorPreview.activeCurveMode")) ||
      !studio_workspace_source.contains(QStringLiteral("return editorPreview.activeCurvePoints")) ||
      !studio_workspace_source.contains(QStringLiteral("return \"Neutral\"")) ||
      !studio_workspace_source.contains(QStringLiteral("return root.defaultCurvePoints()")) ||
      !studio_workspace_source.contains(QStringLiteral("return editorPreview.setPhotoLayerCurvePoints(layerIndex, editorPreview.curveChannel, points)")) ||
      !studio_workspace_source.contains(QStringLiteral("function resetActiveAdjustmentLayerRecipe()")) ||
      !studio_workspace_source.contains(QStringLiteral("return editorPreview.resetPhotoLayerAdjustments(layerIndex)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeLayerFullResetButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"pasteGlobalAdjustmentsToPhotoLayerButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.pasteGlobalAdjustmentsToPhotoLayer(photoLayerAdjustmentCommandBar.activeAdjustmentIndex)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"togglePhotoLayerAdjustmentsPreviewButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.setPhotoLayerAdjustmentsPreviewBypassed("))) {
    return fail("Studio tone curve routing should distinguish Base Image curves from neutral active-layer curves");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function activeEditTargetVisualSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("property var selectedEditTarget: ({ scope: \"base-image\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string selectedEditTargetContract: selectedEditTargetSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("function selectedEditTargetSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("function setSelectedEditTarget(scope, layerIndex, maskSet, sourceIndex)")) ||
      !studio_workspace_source.contains(QStringLiteral("return \"selected-target:v1|scope:\" + root.selectedEditTargetScope()")) ||
      !studio_workspace_source.contains(QStringLiteral("root.setSelectedEditTarget(\"base-image\", -1, ({}), -1)")) ||
      !studio_workspace_source.contains(QStringLiteral("root.setSelectedEditTarget(root.selectedLayerTargetScope(index), index, ({}), -1)")) ||
      !studio_workspace_source.contains(QStringLiteral("root.setSelectedEditTarget(showAdjustments && layerIndex >= 0 ? root.selectedLayerTargetScope(layerIndex) : \"mask\"")) ||
      !studio_workspace_source.contains(QStringLiteral("\"|selected-target:\" + root.selectedEditTargetContract")) ||
      !studio_workspace_source.contains(QStringLiteral("function activeMaskSetData()")) ||
      !studio_workspace_source.contains(QStringLiteral("function activeEditTargetThumbnailData()")) ||
      !studio_workspace_source.contains(QStringLiteral("function activeEditTargetBadgeText()")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeEditTargetAccentRail\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeEditTargetScopeBadge\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeEditTargetThumbnail\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeEditTargetThumbnailImage\"")) ||
      !studio_workspace_source.contains(QStringLiteral("|rail:true|separation:hard")) ||
      !studio_workspace_source.contains(QStringLiteral("|thumbnail:\" + String(scope !== \"base-image\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string visualContract: root.activeEditTargetVisualSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("border.width: targetScope === \"base-image\" ? theme.chromeBorderWidth : 2"))) {
    return fail("Studio active target bar should use one shared selected-target state for Base Image, layer, mask, canvas, and inspector targets");
  }
  if (!studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerMaskSetAdjustButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerMaskSetSelectButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerMaskSetPreviewButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskEditLayerButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskSelectButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskPreviewButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.setMaskSetPreviewBypassed(")) ||
      !studio_workspace_source.contains(QStringLiteral("text: \"Edit Layer\"")) ||
      !studio_workspace_source.contains(QStringLiteral("text: \"Edit Mask\"")) ||
      !studio_workspace_source.contains(QStringLiteral("role: \"segmented\"")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: root.selectMaskSetAndLayer(modelData, true)")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: root.selectMaskSetAndLayer(modelData, false)"))) {
    return fail("Studio layer rows should expose explicit Edit Layer and Edit Mask actions for masked layers");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function maskTargetInspectorActive()")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskFirstInspectorSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstInspectorGroup\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstOverlayButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstOutlineButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstHiddenButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstEditLayerButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("root.selectMaskSetAndLayer(root.activeMaskSetData(), true, true)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstVisibilityButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstInvertButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstFeatherSlider\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstDensitySlider\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstOpacitySlider\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstRefineGroup\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstBrushSettingsGroup\"")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: root.editInspectorTab === \"adjust\" && !root.maskTargetInspectorActive() && root.adjustmentGroupMatches(\"white-balance\")")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: root.editInspectorTab === \"adjust\" && !root.maskTargetInspectorActive() && root.adjustmentGroupMatches(\"light\")")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: root.editInspectorTab === \"adjust\" && !root.maskTargetInspectorActive() && root.adjustmentGroupMatches(\"color\")")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: root.editInspectorTab === \"adjust\" && !root.maskTargetInspectorActive() && root.adjustmentGroupMatches(\"tone-curve\")"))) {
    return fail("Studio Adjust inspector should show mask controls first and hide unrelated color controls while a mask is selected");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function layerTargetInspectorActive()")) ||
      !studio_workspace_source.contains(QStringLiteral("function activeLayerLinkedMaskSets()")) ||
      !studio_workspace_source.contains(QStringLiteral("function activeLayerMaskChildrenSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeLayerMaskChildrenGroup\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeLayerMaskChildRow\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeLayerMaskChildThumbnail\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeLayerMaskChildEditMaskButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: root.layerTargetInspectorActive() && activeLayerMaskChildrenGroup.linkedMaskSets.length > 0")) ||
      !studio_workspace_source.contains(QStringLiteral("full layer adjustments below")) ||
      !studio_workspace_source.contains(QStringLiteral("root.selectMaskSetAndLayer(modelData, false, true)"))) {
    return fail("Studio Adjust inspector should keep linked masks visible as children while a layer target shows the full adjustment stack");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function layerChangedAdjustmentGroups(layer)")) ||
      !studio_workspace_source.contains(QStringLiteral("function layerChangedAdjustmentGroupSignature(layer)")) ||
      !studio_workspace_source.contains(QStringLiteral("function layerLocalAdjustmentSummary(layer)")) ||
      !studio_workspace_source.contains(QStringLiteral("function layerDominantHslChannelName(layer)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerChangedGroupBadgeRow\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerChangedGroupBadge\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerAdjustmentSummaryLabel\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string changedAdjustmentGroupSignature: root.layerChangedAdjustmentGroupSignature(layerData)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string localAdjustmentSummary: root.layerLocalAdjustmentSummary(layerData)")) ||
      !studio_workspace_source.contains(QStringLiteral("function appendGroup(key, label)")) ||
      !studio_workspace_source.contains(QStringLiteral("appendGroup(\"tone-curve\", \"Curve\")")) ||
      !studio_workspace_source.contains(QStringLiteral("appendGroup(\"hsl\", \"HSL\")")) ||
      !studio_workspace_source.contains(QStringLiteral("appendGroup(\"grading\", \"Grade\")")) ||
      !studio_workspace_source.contains(QStringLiteral("appendGroup(\"lut\", \"LUT\")")) ||
      !studio_workspace_source.contains(QStringLiteral("String(layer && layer.changedAdjustmentGroupLabel || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("parts.push(hslChannel.length > 0 ? \"HSL \" + hslChannel : \"HSL\")")) ||
      !studio_workspace_source.contains(QStringLiteral("parts.push(\"Exposure \" + root.signedLayerValueText(root.layerStoredAdjustmentValue(layer, \"exposure\"), \" EV\"))"))) {
    return fail("Studio layer rows should expose changed adjustment group badges and a layer-local adjustment summary");
  }
  if (!studio_workspace_source.contains(QStringLiteral("objectName: \"activeEditTargetReturnToBaseButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: activeEditTargetBar.targetScope !== \"base-image\"")) ||
      !studio_workspace_source.contains(QStringLiteral("const retainedLayerSelection = root.normalizedPhotoLayerSelection()")) ||
      !studio_workspace_source.contains(QStringLiteral("root.selectedPhotoLayerIndices = retainedLayerSelection")) ||
      !studio_workspace_source.contains(QStringLiteral("tooltip: \"Return to Base Image editing without closing the active layer or selection.\"")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: root.returnToBaseImageFromShortcut()"))) {
    return fail("Studio active target bar should expose a clear Return to Base Image control");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function adjustmentGroupFilterText(groupKey)")) ||
      !studio_workspace_source.contains(QStringLiteral("function adjustmentGroupEditStack(groupKey)")) ||
      !studio_workspace_source.contains(QStringLiteral("function activateLayerAdjustmentGroup(layerIndex, groupKey)")) ||
      !studio_workspace_source.contains(QStringLiteral("root.setActivePhotoLayerSelection(index, false)")) ||
      !studio_workspace_source.contains(QStringLiteral("root.editAdjustmentFilterText = root.adjustmentGroupFilterText(groupKey)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerChangedGroupBadgeTargetArea\"")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: root.activateLayerAdjustmentGroup(photoLayerRowRoot.layerIndex, parent.groupKey)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerMaskSetThumbnailTargetArea\"")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: root.selectMaskSetAndLayer(photoLayerMaskSetRowRoot.modelData, false)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskThumbnailTargetArea\"")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: root.selectMaskSetAndLayer(photoLayerNestedMaskRowRoot.modelData, false)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeLayerMaskChildThumbnailTargetArea\"")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: root.selectMaskSetAndLayer(activeLayerMaskChildRowRoot.modelData, false)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function outlinePathsHitTest(mouseX, mouseY, outlinePaths, slopPixels)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function selectActiveOutlineFromCanvas(mouseX, mouseY)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("objectName: \"maskCanvasActiveOutlineHitTarget\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("selectionSyncContract: \"canvas-outline-hit-test|selectMaskSetAndLayer|preserve-inspector:v1\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editRoot.selectMaskSetAndLayer(editRoot.activeMaskSetData(), false, true)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("signal activeOutlineClicked(real normalizedX, real normalizedY)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("function outlinePathsHitTest(mouseX, mouseY, outlinePaths, slopPixels)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("function selectActiveOutlineFromCanvas(mouseX, mouseY)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("SharedActiveOutlineHitTarget")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("selectionSyncContract: \"canvas-outline-hit-test|activeOutlineClicked|selectMaskSetAndLayer:v1\"")) ||
      !studio_workspace_source.contains(QStringLiteral("onActiveOutlineClicked: root.selectMaskSetAndLayer(root.activeMaskSetData(), false, true)"))) {
    return fail("Studio layer and mask rows should provide explicit synchronized click targets for badges and thumbnails");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function activateBaseImageLayerTarget()")) ||
      !studio_workspace_source.contains(QStringLiteral("function globalChangedAdjustmentGroups()")) ||
      !studio_workspace_source.contains(QStringLiteral("function globalAdjustmentSummary()")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"baseImageLayerRowTargetArea\"")) ||
      !studio_workspace_source.contains(QStringLiteral("clickRouteContract: \"base-image-row-click:activate-base-target:v1\"")) ||
      !studio_workspace_source.contains(QStringLiteral("function triggerBaseImageLayerRowClickForTest()")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: root.activateBaseImageLayerTarget()")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.baseImageThumbnailSource")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"baseImageLayerThumbnailImage\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"baseImageLayerLockBadge\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"baseImageLayerMetadataLabel\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"baseImageGlobalAdjustmentBadge\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string baseLayerTargetKind: \"global-edits\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string baseLayerContract: \"base-image-layer:v1|locked:true|stack:bottom|target:\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string changedAdjustmentGroupSignature: root.globalChangedAdjustmentGroupSignature()"))) {
    return fail("Studio Layers panel should expose Base Image as a selectable locked global-edit row");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function unlinkedMaskSets()")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskSetIdentity(maskSet)")) ||
      !studio_workspace_source.contains(QStringLiteral("function linkedMaskSetIdentities()")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskSetListPartitionSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("function photoLayerMaskSetIds(layer)")) ||
      !studio_workspace_source.contains(QStringLiteral("function photoLayerMaskSetNames(layer)")) ||
      !studio_workspace_source.contains(QStringLiteral("function normalizedPhotoLayerMaskSetValues(layer, listKey, singleKey)")) ||
      !studio_workspace_source.contains(QStringLiteral("function moveLayerMaskSet(layerIndex, maskSet, targetMaskIndex)")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.moveMaskSetInPhotoLayer")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property var unlinkedMaskSets: root.unlinkedMaskSets()")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string standaloneMaskListContract: \"standalone-mask-list:unlinked-only|count:\" + unlinkedMaskSets.length")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskListPartitionSignature: root.maskSetListPartitionSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: unlinkedMaskSets.length > 0")) ||
      !studio_workspace_source.contains(QStringLiteral("model: photoLayersMaskSetList.sectionCollapsed ? [] : photoLayersMaskSetList.unlinkedMaskSets")) ||
      !studio_workspace_source.contains(QStringLiteral("id: photoLayerNestedMaskList")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskList\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property var linkedMaskSets: root.maskSetsForAdjustmentLayer(layerIndex)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property var linkedMaskSetIdentities: root.maskSetIdentityList(linkedMaskSets)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string nestedMaskListContract: \"nested-mask-list:linked-only|layer:\"")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: linkedMaskSets.length > 0")) ||
      !studio_workspace_source.contains(QStringLiteral("model: photoLayerNestedMaskList.maskSectionCollapsed ? [] : photoLayerNestedMaskList.linkedMaskSets")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskSourceIconName(kindName)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskTypeIcon\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string combineModeSummary: String(modelData.combineModeSummary || \"Add\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string combineModeContract: String(modelData.combineModeContract || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string combineOrderLabel: (combineOrderIndex + 1).toString() + \" / \" + combineOrderCount.toString()")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskOrderLabel\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskMoveUpButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskMoveDownButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskChildRowContract: \"nested-mask-child:v1|indent:\""))) {
    return fail("Studio Layers panel should nest linked masks under layers and reserve the standalone list for unlinked masks");
  }
  if (!studio_workspace_source.contains(QStringLiteral("property bool standaloneMaskSectionCollapsed: false")) ||
      !studio_workspace_source.contains(QStringLiteral("property var collapsedLayerMaskSections: ({})")) ||
      !studio_workspace_source.contains(QStringLiteral("function photoLayerMaskSectionKey(layerIndex, layer)")) ||
      !studio_workspace_source.contains(QStringLiteral("function photoLayerMaskSectionCollapsed(layerIndex, layer)")) ||
      !studio_workspace_source.contains(QStringLiteral("function setPhotoLayerMaskSectionCollapsed(layerIndex, layer, collapsed)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string standaloneMaskSectionContract: \"mask-section:v1|scope:unlinked|count:\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayersMaskSetSectionHeader\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayersMaskSetSectionToggle\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSectionContract: \"mask-section:v1|scope:layer|key:\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskSectionHeader\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskSectionLabel\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskSectionToggle\"")) ||
      !studio_workspace_source.contains(QStringLiteral("model: photoLayerNestedMaskList.maskSectionCollapsed ? [] : photoLayerNestedMaskList.linkedMaskSets"))) {
    return fail("Studio Layers panel should expose collapsible mask sections for complex layer stacks");
  }
  if (!studio_workspace_source.contains(QStringLiteral("readonly property string finalAlphaSourceKind: String(modelData.mask_source_kind || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string finalAlphaTraceSignature: String(modelData.mask_alpha_trace_signature || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool finalAlphaComposite: finalAlphaSourceKind === \"composite\" &&")) ||
      !studio_workspace_source.contains(QStringLiteral("finalAlphaTraceSignature.indexOf(\"mask-thumbnail-composite\") >= 0"))) {
    return fail("Studio mask thumbnails should expose final composite alpha provenance");
  }
  if (!studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailRenderPath: String(layerData.thumbnail_render_stage_contract || layerData.thumbnailRenderStageContract || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailAlphaTraceSignature: String(layerData.mask_alpha_trace_signature || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailDisplaySource: String(layerData.thumbnailDisplaySource || thumbnailSource)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailEffectPreviewContract: String(layerData.thumbnailEffectPreviewContract || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool thumbnailUsesEffectPreview: thumbnailEffectPreviewSource.length > 0 && thumbnailDisplaySource === thumbnailEffectPreviewSource")) ||
      !studio_workspace_source.contains(QStringLiteral("source: parent.thumbnailDisplaySource")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailInvalidationColorPipeline: String(layerData.thumbnailInvalidationColorPipelineVersion || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool layerThumbnailCompositePath: layerThumbnailAlphaTraceSignature.indexOf(\"layer-thumbnail-composite\") >= 0")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool layerThumbnailColorManaged: layerThumbnailInvalidationColorPipeline.length > 0")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailPathSignature: \"layer-thumbnail|stage:\" + layerThumbnailStage + \"|composite:\" + String(layerThumbnailCompositePath) + \"|color:\" + layerThumbnailInvalidationColorPipeline + \"|key:\" + layerThumbnailInvalidationKey"))) {
    return fail("Studio layer thumbnails should expose composited render path and color-management provenance");
  }
  if (!studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailInvalidationKey: String(layerData.thumbnailInvalidationKey || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailInvalidationRecipeHash: String(layerData.thumbnailInvalidationRecipeHash || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailInvalidationMaskAlphaHash: String(layerData.thumbnailInvalidationMaskAlphaHash || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailInvalidationBlendMode: String(layerData.thumbnailInvalidationBlendMode || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailInvalidationOpacity: String(layerData.thumbnailInvalidationOpacity || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool layerThumbnailInvalidationVisible: layerData.thumbnailInvalidationVisible === true")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailInvalidationKey: String(modelData.thumbnailInvalidationKey || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailInvalidationMaskAlphaHash: String(modelData.thumbnailInvalidationMaskAlphaHash || \"\")"))) {
    return fail("Studio thumbnail rows should expose thumbnail invalidation identity fields");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function thumbnailScreenDevicePixelRatio()")) ||
      !studio_workspace_source.contains(QStringLiteral("function thumbnailSourceSizeForDisplay(displayPixels, backingPixels)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property int screenAwareThumbnailBackingWidth: root.thumbnailSourceSizeForDisplay(32, thumbnailBackingWidth)")) ||
      !studio_workspace_source.contains(QStringLiteral("sourceSize.width: parent.screenAwareThumbnailBackingWidth")) ||
      !studio_workspace_source.contains(QStringLiteral("sourceSize.height: parent.screenAwareThumbnailBackingHeight"))) {
    return fail("Studio thumbnails should bind decode source size to current screen device pixel ratio");
  }
  if (!studio_workspace_source.contains(QStringLiteral("readonly property bool thumbnailProgressive: layerData.thumbnailProgressive === true")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailProgressiveReason: String(layerData.thumbnailProgressiveReason || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailProgressiveSignature: String(layerData.thumbnailProgressiveSignature || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool thumbnailProgressive: modelData.thumbnailProgressive === true")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool thumbnailProgressive: photoLayerMaskSetRowRoot.thumbnailProgressive")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool thumbnailProgressive: photoLayerNestedMaskRowRoot.thumbnailProgressive"))) {
    return fail("Studio thumbnail rows should expose progressive last-good thumbnail state");
  }
  if (!studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailFailureStage: String(layerData.thumbnailFailureStage || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailFailureDetail: String(layerData.thumbnailFailureDetail || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailFailureSignature: String(layerData.thumbnailFailureSignature || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailFailureStage: String(modelData.thumbnailFailureStage || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailFailureStage: photoLayerMaskSetRowRoot.thumbnailFailureStage")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailFailureStage: photoLayerNestedMaskRowRoot.thumbnailFailureStage"))) {
    return fail("Studio thumbnail rows should expose contained thumbnail failure attribution");
  }
  if (!studio_workspace_source.contains(QStringLiteral("readonly property bool thumbnailRefreshQueued: thumbnailProgressive && thumbnailStale && thumbnailProgressiveReason === \"thumbnail_refresh_deferred\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailQueueSignature: \"thumbnail-queue|deferred:\" + String(thumbnailRefreshQueued) + \"|reason:\" + thumbnailProgressiveReason")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool thumbnailRefreshQueued: photoLayerRowRoot.thumbnailRefreshQueued")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool thumbnailRefreshQueued: photoLayerMaskSetRowRoot.thumbnailRefreshQueued")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool thumbnailRefreshQueued: photoLayerNestedMaskRowRoot.thumbnailRefreshQueued"))) {
    return fail("Studio thumbnail rows should expose deferred thumbnail queue state");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function thumbnailRetryActionSignature(data, rowKind)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerThumbnailRetryButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerMaskSetThumbnailRetryButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeLayerMaskChildThumbnailRetryButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskThumbnailRetryButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: editorPreview.retryThumbnailRender()"))) {
    return fail("Studio thumbnail rows should expose a contained, non-blocking retry action for failed thumbnails");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function maskAlphaProviderSummary(data)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskAlphaProviderContract: String(modelData.mask_alpha_provider_contract || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool maskAlphaFullResolutionAvailable: modelData.mask_alpha_full_resolution_available === true")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool maskAlphaDeterministic: modelData.mask_alpha_deterministic === true")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskAlphaProviderSignature: String(modelData.mask_alpha_provider_signature || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskAlphaProviderSummary: root.maskAlphaProviderSummary(modelData)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailAlphaProviderContract: String(layerData.mask_alpha_provider_contract || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool layerThumbnailAlphaFullResolutionAvailable: layerData.mask_alpha_full_resolution_available === true")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailAlphaProviderSummary: root.maskAlphaProviderSummary(layerData)"))) {
    return fail("Studio mask and layer rows should expose shared alpha-provider provenance");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function maskSourceProvenanceSummary(data)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSourceBrushContract: String(modelData.brushAlphaSourceContract || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property int maskSourceBrushStrokeCount: Math.max(0, Number(modelData.brushAlphaStrokeCount || 0))")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSourceBrushSignature: String(modelData.brushAlphaSourceSignature || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSourceShapeContract: String(modelData.shapeAlphaSourceContract || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool maskSourceShapeEditable: modelData.shapeAlphaEditable === true")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSourceShapeSignature: String(modelData.shapeAlphaSourceSignature || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSourceGeneratedContract: String(modelData.generatedAlphaSourceContract || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool maskSourceGeneratedConfidenceAvailable: modelData.generatedAlphaConfidenceAvailable === true")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSourceGeneratedRefinementSignature: String(modelData.generatedAlphaRefinementStateSignature || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSourceProvenanceSummary: root.maskSourceProvenanceSummary(modelData)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSourceProvenanceSummary: photoLayerMaskSetRowRoot.maskSourceProvenanceSummary")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSourceProvenanceSummary: photoLayerNestedMaskRowRoot.maskSourceProvenanceSummary"))) {
    return fail("Studio mask rows should expose brush, editable path, and generated source provenance");
  }
  engine.load(QUrl::fromLocalFile(main_qml_path));
  QCoreApplication::processEvents();

  if (engine.rootObjects().isEmpty()) {
    return fail("desktop shell should load Main.qml");
  }

  QObject* root = engine.rootObjects().constFirst();
  if (root == nullptr) {
    return fail("desktop shell should expose a root object");
  }
  auto* root_window = qobject_cast<QQuickWindow*>(root);
  if (root_window == nullptr) {
    return fail("desktop shell visual proof requires the root QML object to be a window");
  }
  const auto visual_proof_dir = qEnvironmentVariableIsSet("PHOTO_EDITOR_VISUAL_PROOF_DIR")
                                    ? QString::fromUtf8(qgetenv("PHOTO_EDITOR_VISUAL_PROOF_DIR"))
                                    : runtime_root.filePath(QStringLiteral("desktop-visual-proof"));
  if (!QDir().mkpath(visual_proof_dir)) {
    return fail("desktop shell visual proof should create its artifact directory");
  }
  QJsonArray visual_proof_manifest;
  root->setProperty("width", 1600);
  root->setProperty("height", 1000);
  process_layout_events(6);

  root->setProperty("currentWorkspace", root->property("editWorkspaceIndex"));
  QCoreApplication::processEvents();

  catalog_assets.replaceAssetsForTesting(
      {make_asset(QStringLiteral("asset-1"), QStringLiteral("IMG_0001.jpg"))});
  catalog_assets.selectIndex(0);
  QCoreApplication::processEvents();
  if (editor_preview.assetId() != QStringLiteral("asset-1") || !editor_preview.hasAsset()) {
    return fail("edit workspace should load the selected catalog asset");
  }

  catalog_assets.replaceAssetsForTesting({});
  QCoreApplication::processEvents();
  if (!editor_preview.assetId().isEmpty() || editor_preview.hasAsset()) {
    return fail("edit workspace should clear the editor when the catalog selection disappears");
  }

  catalog_assets.replaceAssetsForTesting(
      {make_asset(QStringLiteral("asset-2"), QStringLiteral("IMG_0002.jpg"))});
  QCoreApplication::processEvents();
  if (catalog_assets.selectedAssetId() != QStringLiteral("asset-2") ||
      editor_preview.assetId() != QStringLiteral("asset-2")) {
    return fail("edit workspace should follow the next visible asset after the gallery repopulates");
  }

  auto* edit_workspace = root->findChild<QObject*>(QStringLiteral("editStudioWorkspace"));
  if (edit_workspace == nullptr) {
    return fail("edit workspace should expose a testable StudioWorkspace object");
  }
  auto* workspace_tab_edit = find_named_object(root, QStringLiteral("workspaceTabEdit"));
  auto* workspace_tab_edit_label = find_named_object(root, QStringLiteral("workspaceTabLabelEdit"));
  auto* top_workspace_status_label = find_named_object(root, QStringLiteral("topWorkspaceStatusLabel"));
  auto* top_selection_status_label = find_named_object(root, QStringLiteral("topSelectionStatusLabel"));
  if (workspace_tab_edit == nullptr || workspace_tab_edit_label == nullptr ||
      top_workspace_status_label == nullptr || top_selection_status_label == nullptr ||
      workspace_tab_edit->property("text").toString() != QStringLiteral("Edit") ||
      workspace_tab_edit_label->property("text").toString() != QStringLiteral("Edit") ||
      top_workspace_status_label->property("text").toString().startsWith(QStringLiteral("EDIT")) ||
      top_selection_status_label->property("text").toString() == QStringLiteral("NO ACTIVE ASSET")) {
    return fail("global workspace chrome should use readable title-case navigation and status labels");
  }

  const auto viewport_asset_path = runtime_root.filePath(QStringLiteral("viewport-qml-proof.png"));
  if (!write_overlay_visual_fixture(viewport_asset_path, QStringLiteral("base"))) {
    return fail("viewport QML proof should create a readable test image");
  }
  const auto bright_overlay_asset_path =
      runtime_root.filePath(QStringLiteral("overlay-bright-proof.png"));
  const auto dark_overlay_asset_path = runtime_root.filePath(QStringLiteral("overlay-dark-proof.png"));
  const auto saturated_overlay_asset_path =
      runtime_root.filePath(QStringLiteral("overlay-saturated-proof.png"));
  const auto transparent_overlay_asset_path =
      runtime_root.filePath(QStringLiteral("overlay-transparent-proof.png"));
  if (!write_overlay_visual_fixture(bright_overlay_asset_path, QStringLiteral("bright")) ||
      !write_overlay_visual_fixture(dark_overlay_asset_path, QStringLiteral("dark")) ||
      !write_overlay_visual_fixture(saturated_overlay_asset_path, QStringLiteral("saturated")) ||
      !write_overlay_visual_fixture(transparent_overlay_asset_path, QStringLiteral("transparent"))) {
    return fail("overlay visual proof should create bright, dark, saturated, and transparent fixtures");
  }
  catalog_assets.replaceAssetsForTesting({make_asset(
      QStringLiteral("viewport-qml-proof"),
      viewport_asset_path,
      QUrl::fromLocalFile(viewport_asset_path).toString())});
  catalog_assets.selectIndex(0);
  if (!wait_for_condition([&]() {
        return editor_preview.hasAsset() &&
               editor_preview.assetId() == QStringLiteral("viewport-qml-proof");
      })) {
    return fail("viewport QML proof asset should load into the editor");
  }
  auto* edit_viewport = find_named_object(edit_workspace, QStringLiteral("editImageViewport"));
  if (edit_viewport == nullptr ||
      !QMetaObject::invokeMethod(
          edit_viewport,
          "setZoom",
          Q_ARG(QVariant, QVariant(2.0)))) {
    return fail("edit ImageViewport should expose a callable zoom path");
  }
  if (!wait_for_condition([&]() {
        return editor_preview.previewViewportActive() &&
               editor_preview.previewViewportRect().isValid();
      })) {
    return fail("StudioWorkspace zoom should publish an active preview viewport to the controller");
  }
  const auto zoom_viewport_rect = editor_preview.previewViewportRect();
  if (zoom_viewport_rect.width() <= 0.0 || zoom_viewport_rect.height() <= 0.0 ||
      zoom_viewport_rect.width() > 1.0 || zoom_viewport_rect.height() > 1.0 ||
      (zoom_viewport_rect.width() >= 0.99 && zoom_viewport_rect.height() >= 0.99) ||
      editor_preview.previewViewportPatchMaxEdge() <= 0) {
    return fail("StudioWorkspace zoom should publish a bounded normalized source viewport");
  }
  edit_viewport->setProperty("panX", edit_viewport->property("panX").toDouble() - 96.0);
  edit_viewport->setProperty("panY", edit_viewport->property("panY").toDouble() - 64.0);
  if (!wait_for_condition([&]() {
        return editor_preview.previewViewportActive() &&
               editor_preview.previewViewportRect() != zoom_viewport_rect;
      })) {
    return fail("StudioWorkspace pan should update the published preview viewport");
  }
  editor_preview.setPreviewViewport(0, 0, 0, 0, 0, false);
  catalog_assets.replaceAssetsForTesting(
      {make_asset(QStringLiteral("asset-2"), QStringLiteral("IMG_0002.jpg"))});
  if (!wait_for_condition([&]() {
        return editor_preview.assetId() == QStringLiteral("asset-2") && !editor_preview.processing();
      }, 10000)) {
    return fail("viewport QML proof should restore the lightweight smoke asset before continuing");
  }

  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  for (auto iteration = 0; iteration < 6; ++iteration) {
    QCoreApplication::processEvents();
  }
  QVariant cool_kelvin;
  QVariant neutral_kelvin;
  QVariant warm_kelvin;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "whiteBalanceKelvin",
          Q_RETURN_ARG(QVariant, cool_kelvin),
          Q_ARG(QVariant, QVariant(-100))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "whiteBalanceKelvin",
          Q_RETURN_ARG(QVariant, neutral_kelvin),
          Q_ARG(QVariant, QVariant(0))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "whiteBalanceKelvin",
          Q_RETURN_ARG(QVariant, warm_kelvin),
          Q_ARG(QVariant, QVariant(100))) ||
      !(cool_kelvin.toInt() < neutral_kelvin.toInt() &&
        neutral_kelvin.toInt() < warm_kelvin.toInt())) {
    return fail("temperature slider Kelvin labels should increase in the warm direction");
  }

  const auto expect_slider = [&](const char* object_name,
                                 double expected_from,
                                 double expected_to,
                                 double expected_default,
                                 const char* failure_message) -> int {
    auto* slider = edit_workspace->findChild<QObject*>(QString::fromLatin1(object_name));
    if (slider == nullptr) {
      std::cerr << failure_message << ": slider should exist\n";
      return EXIT_FAILURE;
    }
    const auto actual_from = slider->property("from").toDouble();
    const auto actual_to = slider->property("to").toDouble();
    const auto actual_default = slider->property("defaultValue").toDouble();
    const auto modified = slider->property("modified").toBool();
    if (std::abs(actual_from - expected_from) > 1e-6 || std::abs(actual_to - expected_to) > 1e-6 ||
        std::abs(actual_default - expected_default) > 1e-6 || modified) {
      std::cerr << failure_message << ": expected range/default "
                << expected_from << ".." << expected_to << " default " << expected_default
                << ", got " << actual_from << ".." << actual_to << " default " << actual_default
                << ", modified=" << modified << "\n";
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  };
  if (expect_slider("selectedToneWheelHueSlider", 0.0, 1.0, 0.0, "color grading hue slider") !=
          EXIT_SUCCESS ||
      expect_slider("toneCurveInputBlackSlider", 0.0, 95.0, 0.0, "tone curve input black slider") !=
          EXIT_SUCCESS ||
      expect_slider("toneCurveInputGammaSlider", -100.0, 100.0, 0.0, "tone curve input gamma slider") !=
          EXIT_SUCCESS ||
      expect_slider("toneCurveInputWhiteSlider", 5.0, 100.0, 100.0, "tone curve input white slider") !=
          EXIT_SUCCESS ||
      expect_slider("channelMixerRedFromRedSlider", -200.0, 200.0, 100.0, "channel mixer identity slider") !=
          EXIT_SUCCESS ||
      expect_slider("colorReplacementTargetHueSlider", 0.0, 1.0, 0.33, "color replacement target hue slider") !=
          EXIT_SUCCESS ||
      expect_slider("colorReplacementRangeSlider", 1.0, 100.0, 35.0, "color replacement range slider") !=
          EXIT_SUCCESS ||
      expect_slider("colorReplacementSoftnessSlider", 0.0, 100.0, 100.0, "color replacement softness slider") !=
          EXIT_SUCCESS ||
      expect_slider("colorReplaceSkinHueSlider", 0.0, 1.0, 0.08, "color replacement protected skin hue slider") !=
          EXIT_SUCCESS ||
      expect_slider("colorReplaceSkinRangeSlider", 1.0, 100.0, 30.0, "color replacement protected skin range slider") !=
          EXIT_SUCCESS ||
      expect_slider("colorReplaceSkinProtectionSlider", 0.0, 100.0, 0.0, "color replacement skin protection slider") !=
          EXIT_SUCCESS ||
      expect_slider("skinToneLineHueSlider", 0.0, 1.0, 0.08, "skin tone line hue slider") != EXIT_SUCCESS ||
      expect_slider("cropZoomSlider", 0.5, 4.0, 1.0, "crop zoom slider") != EXIT_SUCCESS ||
      expect_slider("cropRectXSlider", 0.0, 100.0, 0.0, "crop rectangle x slider") !=
          EXIT_SUCCESS ||
      expect_slider("cropRectYSlider", 0.0, 100.0, 0.0, "crop rectangle y slider") !=
          EXIT_SUCCESS ||
      expect_slider("cropRectWidthSlider", 5.0, 100.0, 100.0, "crop rectangle width slider") !=
          EXIT_SUCCESS ||
      expect_slider("cropRectHeightSlider", 5.0, 100.0, 100.0, "crop rectangle height slider") !=
          EXIT_SUCCESS ||
      expect_slider("transformScaleSlider", 0.25, 4.0, 1.0, "transform scale slider") != EXIT_SUCCESS ||
      expect_slider("detailSharpenRadiusSlider", 0.5, 3.0, 1.0, "detail sharpen radius slider") !=
          EXIT_SUCCESS ||
      expect_slider("detailSharpenDetailSlider", 0.0, 100.0, 0.0, "detail sharpen detail slider") !=
          EXIT_SUCCESS ||
      expect_slider("liquifyRadiusSlider", 0.02, 1.0, 0.25, "liquify radius slider") != EXIT_SUCCESS) {
    return EXIT_FAILURE;
  }
  if (edit_workspace->findChild<QObject*>(QStringLiteral("channelMixerIdentityPresetButton")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("channelMixerMonochromePresetButton")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("channelMixerSwapRedBluePresetButton")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("channelMixerResetRedRowButton")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("channelMixerResetGreenRowButton")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("channelMixerResetBlueRowButton")) == nullptr) {
    return fail("channel mixer should expose presets and per-row reset controls");
  }
  if (edit_workspace->findChild<QObject*>(QStringLiteral("colorReplaceProtectSkinCheckBox")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("colorReplacementPreserveLuminanceCheckBox")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("colorReplacementPreserveSaturationCheckBox")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("colorReplaceMaskPreviewButton")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("colorReplaceAutoSkinButton")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("colorToolMaskPreviewOverlay")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("colorReplaceSkinHueSlider")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("colorReplaceSkinRangeSlider")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("colorReplaceSkinProtectionSlider")) == nullptr) {
    return fail("color replace should expose preservation and skin protection controls next to replacement controls");
  }
  auto* white_balance_group = edit_workspace->findChild<QObject*>(QStringLiteral("whiteBalanceInspectorGroup"));
  auto* light_group = edit_workspace->findChild<QObject*>(QStringLiteral("lightInspectorGroup"));
  auto* tone_curve_group = edit_workspace->findChild<QObject*>(QStringLiteral("toneCurveInspectorGroup"));
  auto* color_group = edit_workspace->findChild<QObject*>(QStringLiteral("colorInspectorGroup"));
  auto* presence_group = edit_workspace->findChild<QObject*>(QStringLiteral("presenceInspectorGroup"));
  auto* raw_group = find_named_object(edit_workspace, QStringLiteral("rawInspectorGroup"));
  auto* edit_inspector_panel = edit_workspace->findChild<QObject*>(QStringLiteral("editInspectorPanel"));
  auto* adjust_inline_histogram_scope =
      find_named_object(edit_workspace, QStringLiteral("adjustInlineHistogramScope"));
  auto* adjust_inline_waveform_scope =
      find_named_object(edit_workspace, QStringLiteral("adjustInlineWaveformScope"));
  auto* adjust_inspector_tab = find_named_object(edit_workspace, QStringLiteral("adjustInspectorTabButton"));
  auto* presets_inspector_tab = find_named_object(edit_workspace, QStringLiteral("presetsInspectorTabButton"));
  auto* layers_inspector_tab = find_named_object(edit_workspace, QStringLiteral("layersInspectorTabButton"));
  auto* crop_inspector_tab = find_named_object(edit_workspace, QStringLiteral("cropInspectorTabButton"));
  auto* scopes_inspector_tab = find_named_object(edit_workspace, QStringLiteral("scopesInspectorTabButton"));
  auto* detail_inspector_tab = find_named_object(edit_workspace, QStringLiteral("detailInspectorTabButton"));
  auto* info_inspector_tab = find_named_object(edit_workspace, QStringLiteral("infoInspectorTabButton"));
  auto* photo_layers_group = find_named_object(edit_workspace, QStringLiteral("photoLayersInspectorGroup"));
  auto* adjust_detail_group = find_named_object(edit_workspace, QStringLiteral("detailInspectorGroup"));
  auto* detail_inspect_button = find_named_object(edit_workspace, QStringLiteral("detailInspectOneToOneButton"));
  auto* detail_loupe_button = find_named_object(edit_workspace, QStringLiteral("detailLoupeButton"));
  auto* detail_mask_preview_button =
      find_named_object(edit_workspace, QStringLiteral("detailMaskPreviewButton"));
  auto* detail_before_after_button =
      find_named_object(edit_workspace, QStringLiteral("detailPanelBeforeAfterButton"));
  auto* detail_sharpening_group = find_named_object(edit_workspace, QStringLiteral("detailSharpeningGroup"));
  auto* detail_noise_reduction_group =
      find_named_object(edit_workspace, QStringLiteral("detailNoiseReductionGroup"));
  auto* adjust_lens_group = find_named_object(edit_workspace, QStringLiteral("lensInspectorGroup"));
  auto* light_basic_tone_group = find_named_object(edit_workspace, QStringLiteral("lightBasicToneGroup"));
  auto* light_advanced_tone_group = find_named_object(edit_workspace, QStringLiteral("lightAdvancedToneGroup"));
  auto* light_levels_group = find_named_object(edit_workspace, QStringLiteral("lightLevelsGroup"));
  auto* light_zone_mapping_group = find_named_object(edit_workspace, QStringLiteral("lightZoneMappingGroup"));
  auto* tone_curve_input_levels_group =
      find_named_object(edit_workspace, QStringLiteral("toneCurveInputLevelsGroup"));
  auto* tone_curve_input_levels_strip =
      find_named_object(edit_workspace, QStringLiteral("toneCurveInputLevelsStrip"));
  auto* tone_curve_input_levels_histogram =
      find_named_object(edit_workspace, QStringLiteral("toneCurveInputLevelsHistogram"));
  auto* tone_curve_input_levels_handle =
      find_named_object(edit_workspace, QStringLiteral("toneCurveInputLevelsHandle"));
  auto* light_rolloff_detail_reset_button =
      find_named_object(edit_workspace, QStringLiteral("lightRollOffDetailResetButton"));
  auto* light_zone_mixer_reset_button =
      find_named_object(edit_workspace, QStringLiteral("lightZoneMixerResetButton"));
  auto* tone_curve_input_levels_reset_button =
      find_named_object(edit_workspace, QStringLiteral("toneCurveInputLevelsResetButton"));
  auto* advanced_color_group = find_named_object(edit_workspace, QStringLiteral("advancedColorInspectorGroup"));
  auto* color_grading_group = find_named_object(edit_workspace, QStringLiteral("colorGradingInspectorGroup"));
  auto* color_mix_group = find_named_object(edit_workspace, QStringLiteral("colorMixGroup"));
  auto* color_mix_active_preview =
      find_named_object(edit_workspace, QStringLiteral("colorMixActiveChannelPreview"));
  auto* color_mix_active_swatch =
      find_named_object(edit_workspace, QStringLiteral("colorMixActiveChannelSwatch"));
  auto* color_mix_active_label =
      find_named_object(edit_workspace, QStringLiteral("colorMixActiveChannelLabel"));
  auto* color_mix_active_values =
      find_named_object(edit_workspace, QStringLiteral("colorMixActiveChannelValues"));
  auto* color_mix_reset_channel_button =
      find_named_object(edit_workspace, QStringLiteral("colorMixResetChannelButton"));
  auto* color_mix_affected_range_strip =
      find_named_object(edit_workspace, QStringLiteral("colorMixAffectedRangeStrip"));
  auto* color_mix_affected_range_swatch =
      find_named_object(edit_workspace, QStringLiteral("colorMixAffectedRangeSwatch"));
  auto* channel_mixer_group = find_named_object(edit_workspace, QStringLiteral("channelMixerGroup"));
  auto* channel_mixer_red_sum_label =
      find_named_object(edit_workspace, QStringLiteral("channelMixerRedRowSumLabel"));
  auto* channel_mixer_green_sum_label =
      find_named_object(edit_workspace, QStringLiteral("channelMixerGreenRowSumLabel"));
  auto* channel_mixer_blue_sum_label =
      find_named_object(edit_workspace, QStringLiteral("channelMixerBlueRowSumLabel"));
  auto* black_white_group = find_named_object(edit_workspace, QStringLiteral("blackWhiteGroup"));
  auto* color_replace_group = find_named_object(edit_workspace, QStringLiteral("colorReplaceGroup"));
  auto* color_replace_swatch_preview =
      find_named_object(edit_workspace, QStringLiteral("colorReplaceSwatchPreview"));
  auto* color_replace_source_swatch =
      find_named_object(edit_workspace, QStringLiteral("colorReplaceSourceSwatch"));
  auto* color_replace_target_swatch =
      find_named_object(edit_workspace, QStringLiteral("colorReplaceTargetSwatch"));
  auto* color_replace_summary_label =
      find_named_object(edit_workspace, QStringLiteral("colorReplaceSummaryLabel"));
  auto* color_replace_selection_label =
      find_named_object(edit_workspace, QStringLiteral("colorReplaceSelectionLabel"));
  auto* color_replace_swap_button =
      find_named_object(edit_workspace, QStringLiteral("colorReplaceSwapHueButton"));
  auto* color_replace_mask_preview_button =
      find_named_object(edit_workspace, QStringLiteral("colorReplaceMaskPreviewButton"));
  auto* color_replace_auto_skin_button =
      find_named_object(edit_workspace, QStringLiteral("colorReplaceAutoSkinButton"));
  auto* color_replace_reset_skin_button =
      find_named_object(edit_workspace, QStringLiteral("colorReplaceResetSkinProtectionButton"));
  auto* color_replace_preserve_luminance_check =
      find_named_object(edit_workspace, QStringLiteral("colorReplacementPreserveLuminanceCheckBox"));
  auto* color_replace_preserve_saturation_check =
      find_named_object(edit_workspace, QStringLiteral("colorReplacementPreserveSaturationCheckBox"));
  auto* skin_tone_group = find_named_object(edit_workspace, QStringLiteral("skinToneGroup"));
  auto* harmony_group = find_named_object(edit_workspace, QStringLiteral("harmonyGroup"));
  auto* harmony_palette_preview = find_named_object(edit_workspace, QStringLiteral("harmonyPalettePreview"));
  auto* harmony_anchor_swatch = find_named_object(edit_workspace, QStringLiteral("harmonyAnchorSwatch"));
  auto* harmony_affected_range_strip =
      find_named_object(edit_workspace, QStringLiteral("harmonyAffectedRangeStrip"));
  auto* harmony_affected_range_swatch =
      find_named_object(edit_workspace, QStringLiteral("harmonyAffectedRangeSwatch"));
  auto* harmony_warm_preset_button = find_named_object(edit_workspace, QStringLiteral("harmonyWarmPresetButton"));
  auto* harmony_complement_preset_button =
      find_named_object(edit_workspace, QStringLiteral("harmonyComplementPresetButton"));
  auto* harmony_triad_preset_button = find_named_object(edit_workspace, QStringLiteral("harmonyTriadPresetButton"));
  auto* harmony_base_hue_slider = find_named_object(edit_workspace, QStringLiteral("harmonyBaseHueSlider"));
  auto* harmony_amount_slider = find_named_object(edit_workspace, QStringLiteral("harmonyAmountSlider"));
  auto* split_toning_group = find_named_object(edit_workspace, QStringLiteral("splitToningGroup"));
  auto* hsl_channel_button_row = find_named_object(edit_workspace, QStringLiteral("hslChannelButtonRow"));
  auto* hsl_reds_button = find_named_object(edit_workspace, QStringLiteral("hslChannelRedsButton"));
  auto* hsl_magentas_button = find_named_object(edit_workspace, QStringLiteral("hslChannelMagentasButton"));
  auto* edit_inspector_footer = find_named_object(edit_workspace, QStringLiteral("editInspectorFooter"));
  auto* initial_edit_filmstrip = find_named_item(edit_workspace, QStringLiteral("editFilmstrip"));
  auto* edit_inspector_reset_button = find_named_object(edit_workspace, QStringLiteral("editInspectorResetButton"));
  auto* edit_inspector_save_preset_button =
      find_named_object(edit_workspace, QStringLiteral("editInspectorSavePresetButton"));
  auto* light_panel_before_after_button =
      find_named_object(edit_workspace, QStringLiteral("lightPanelBeforeAfterButton"));
  auto* color_panel_before_after_button =
      find_named_object(edit_workspace, QStringLiteral("colorPanelBeforeAfterButton"));
  auto* presence_panel_before_after_button =
      find_named_object(edit_workspace, QStringLiteral("presencePanelBeforeAfterButton"));
  auto* edit_adjustment_filter_field =
      find_named_object(edit_workspace, QStringLiteral("editAdjustmentFilterField"));
  auto* clear_edit_adjustment_filter_button =
      find_named_object(edit_workspace, QStringLiteral("clearEditAdjustmentFilterButton"));
  auto* edit_adjustment_filter_status_label =
      find_named_object(edit_workspace, QStringLiteral("editAdjustmentFilterStatusLabel"));
  auto* active_edit_target_bar =
      find_named_object(edit_workspace, QStringLiteral("activeEditTargetBar"));
  auto* active_edit_target_mode_label =
      find_named_object(edit_workspace, QStringLiteral("activeEditTargetModeLabel"));
  auto* active_edit_target_breadcrumb_label =
      find_named_object(edit_workspace, QStringLiteral("activeEditTargetBreadcrumbLabel"));
  auto* active_adjustment_layer_target_bar =
      find_named_object(edit_workspace, QStringLiteral("activeAdjustmentLayerTargetBar"));
  auto* active_adjustment_layer_target_name_label =
      find_named_object(edit_workspace, QStringLiteral("activeAdjustmentLayerTargetNameLabel"));
  auto* active_adjustment_layer_target_status_label =
      find_named_object(edit_workspace, QStringLiteral("activeAdjustmentLayerTargetStatusLabel"));
  auto* active_adjustment_layer_open_layers_button =
      find_named_object(edit_workspace, QStringLiteral("activeAdjustmentLayerOpenLayersButton"));
  process_layout_events(6);
  auto* filmstrip_thumbnail_card = find_named_object(edit_workspace, QStringLiteral("editFilmstripThumbnailCard"));
  auto* filmstrip_image_frame = find_named_object(edit_workspace, QStringLiteral("filmstripImageFrame"));
  auto* filmstrip_image = find_named_object(edit_workspace, QStringLiteral("filmstripImage"));
  auto* filmstrip_placeholder = find_named_object(edit_workspace, QStringLiteral("filmstripPlaceholder"));
  auto* filmstrip_selection_indicator =
      find_named_object(edit_workspace, QStringLiteral("filmstripSelectionIndicator"));
  auto* filmstrip_aspect_badge = find_named_object(edit_workspace, QStringLiteral("filmstripAspectBadge"));
  constexpr int kQmlImagePreserveAspectFit = 1;
  if (initial_edit_filmstrip == nullptr || filmstrip_thumbnail_card == nullptr ||
      filmstrip_image_frame == nullptr || filmstrip_image == nullptr || filmstrip_placeholder == nullptr ||
      filmstrip_selection_indicator == nullptr || filmstrip_aspect_badge == nullptr ||
      !item_visible(initial_edit_filmstrip) ||
      !qobject_cast<QQuickItem*>(filmstrip_thumbnail_card)->isVisible() ||
      !filmstrip_thumbnail_card->property("filmstrip").toBool() ||
      filmstrip_thumbnail_card->property("filmstripImageMargin").toInt() != 0 ||
      filmstrip_thumbnail_card->property("filmstripAspectBadgeVisible").toBool() ||
      filmstrip_image->property("fillMode").toInt() != kQmlImagePreserveAspectFit ||
      filmstrip_aspect_badge->property("visible").toBool() ||
      !filmstrip_selection_indicator->property("visible").toBool()) {
    return fail("edit filmstrip thumbnails should preserve aspect ratio with minimal chrome and no in-image aspect badge");
  }
  if (white_balance_group == nullptr || light_group == nullptr || tone_curve_group == nullptr ||
      color_group == nullptr || color_grading_group == nullptr || presence_group == nullptr ||
      adjust_detail_group == nullptr || adjust_lens_group == nullptr || raw_group == nullptr ||
      edit_inspector_footer == nullptr || edit_inspector_reset_button == nullptr ||
      edit_inspector_save_preset_button == nullptr ||
      !white_balance_group->property("visible").toBool() || !light_group->property("visible").toBool() ||
      !tone_curve_group->property("visible").toBool() || !color_group->property("visible").toBool() ||
      !color_grading_group->property("visible").toBool() ||
      !presence_group->property("visible").toBool() ||
      !adjust_detail_group->property("visible").toBool() ||
      !adjust_lens_group->property("visible").toBool() ||
      !(white_balance_group->property("y").toDouble() < light_group->property("y").toDouble() &&
        light_group->property("y").toDouble() < color_group->property("y").toDouble() &&
        color_group->property("y").toDouble() < color_grading_group->property("y").toDouble() &&
        color_grading_group->property("y").toDouble() < tone_curve_group->property("y").toDouble() &&
        tone_curve_group->property("y").toDouble() < presence_group->property("y").toDouble())) {
    return fail("adjust inspector should order primary controls as white balance, light, color, color grading, tone curve, then presence and keep Detail/Lens in Adjust");
  }
  if (edit_inspector_panel == nullptr || adjust_inspector_tab == nullptr || presets_inspector_tab == nullptr ||
      layers_inspector_tab == nullptr ||
      crop_inspector_tab == nullptr ||
      scopes_inspector_tab == nullptr || detail_inspector_tab == nullptr || info_inspector_tab == nullptr ||
      photo_layers_group == nullptr ||
      edit_inspector_panel->property("title").toString() != QStringLiteral("Adjust") ||
      adjust_inline_histogram_scope == nullptr || !adjust_inline_histogram_scope->property("visible").toBool() ||
      adjust_inline_waveform_scope == nullptr || !adjust_inline_waveform_scope->property("visible").toBool() ||
      adjust_inspector_tab->property("text").toString() != QStringLiteral("Adjust") ||
      presets_inspector_tab->property("text").toString() != QStringLiteral("Presets") ||
      layers_inspector_tab->property("text").toString() != QStringLiteral("Layers") ||
      crop_inspector_tab->property("text").toString() != QStringLiteral("Crop") ||
      crop_inspector_tab->property("visible").toBool() ||
      scopes_inspector_tab->property("text").toString() != QStringLiteral("Scopes") ||
      detail_inspector_tab->property("text").toString() != QStringLiteral("Detail") ||
      detail_inspector_tab->property("visible").toBool() ||
      info_inspector_tab->property("text").toString() != QStringLiteral("Info") ||
      photo_layers_group->property("visible").toBool() ||
      light_panel_before_after_button == nullptr || color_panel_before_after_button == nullptr ||
      presence_panel_before_after_button == nullptr ||
      edit_adjustment_filter_field == nullptr || clear_edit_adjustment_filter_button == nullptr ||
      edit_adjustment_filter_status_label == nullptr ||
      active_edit_target_bar == nullptr || active_edit_target_mode_label == nullptr ||
      active_edit_target_breadcrumb_label == nullptr ||
      active_adjustment_layer_target_bar == nullptr ||
      active_adjustment_layer_target_name_label == nullptr ||
      active_adjustment_layer_target_status_label == nullptr ||
      active_adjustment_layer_open_layers_button == nullptr ||
      !active_edit_target_bar->property("visible").toBool() ||
      active_edit_target_bar->property("targetScope").toString() != QStringLiteral("base-image") ||
      active_edit_target_breadcrumb_label->property("text").toString() != QStringLiteral("Base Image") ||
      active_edit_target_mode_label->property("text").toString() != QStringLiteral("Editing Base Image") ||
      active_adjustment_layer_target_bar->property("visible").toBool() ||
      edit_inspector_reset_button->property("text").toString() != QStringLiteral("Reset") ||
      edit_inspector_save_preset_button->property("text").toString() != QStringLiteral("Save Preset") ||
      white_balance_group->property("title").toString() != QStringLiteral("White Balance") ||
      tone_curve_group->property("title").toString() != QStringLiteral("Tone Curve")) {
    return fail("adjust inspector should use readable title-case panel, tab, and group labels");
  }
  QVariant base_target_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activeEditTargetSignature",
          Q_RETURN_ARG(QVariant, base_target_signature)) ||
      base_target_signature.toString() !=
          QStringLiteral("scope:base-image|breadcrumb:Base Image|summary:Global image adjustments")) {
    return fail("adjust inspector should explicitly show Base Image as the global edit target");
  }
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("presets"));
  for (auto iteration = 0; iteration < 6; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* preset_library_group = find_named_object(edit_workspace, QStringLiteral("presetLibraryInspectorGroup"));
  auto* studio_preset_search_field = find_named_object(edit_workspace, QStringLiteral("studioPresetSearchField"));
  auto* studio_preset_category_filter_row =
      find_named_object(edit_workspace, QStringLiteral("studioPresetCategoryFilterRow"));
  auto* studio_preset_pack_filter_combo =
      find_named_object(edit_workspace, QStringLiteral("studioPresetPackFilterCombo"));
  auto* studio_preset_favorites_filter_button =
      find_named_object(edit_workspace, QStringLiteral("studioPresetFavoritesFilterButton"));
  auto* studio_preset_library_list = find_named_object(edit_workspace, QStringLiteral("studioPresetLibraryList"));
  auto* studio_preset_pack_header = find_named_object(edit_workspace, QStringLiteral("studioPresetPackHeader"));
  auto* studio_preset_library_card = find_named_object(edit_workspace, QStringLiteral("studioPresetLibraryCard"));
  auto* studio_preset_favorite_button = find_named_object(edit_workspace, QStringLiteral("studioPresetFavoriteButton"));
  auto* studio_preset_apply_card_button =
      find_named_object(edit_workspace, QStringLiteral("studioPresetApplyFromCardButton"));
  auto* studio_preset_manage_menu_button =
      find_named_object(edit_workspace, QStringLiteral("studioPresetManageMenuButton"));
  auto* studio_preset_management_menu =
      find_named_object(edit_workspace, QStringLiteral("studioPresetManagementMenu"));
  auto* studio_preset_rename_button = find_named_object(edit_workspace, QStringLiteral("studioRenamePresetButton"));
  auto* studio_preset_move_button = find_named_object(edit_workspace, QStringLiteral("studioMovePresetButton"));
  auto* studio_preset_delete_button = find_named_object(edit_workspace, QStringLiteral("studioDeletePresetButton"));
  auto* studio_preset_export_field =
      find_named_object(edit_workspace, QStringLiteral("studioPresetExportFolderField"));
  auto* studio_preset_export_button =
      find_named_object(edit_workspace, QStringLiteral("studioExportPresetBundleButton"));
  auto* studio_preset_import_field =
      find_named_object(edit_workspace, QStringLiteral("studioPresetImportFolderField"));
  auto* studio_preset_import_button =
      find_named_object(edit_workspace, QStringLiteral("studioImportPresetBundleButton"));
  if (edit_inspector_panel->property("title").toString() != QStringLiteral("Presets") ||
      !presets_inspector_tab->property("active").toBool() ||
      preset_library_group == nullptr || !preset_library_group->property("visible").toBool() ||
      studio_preset_search_field == nullptr ||
      studio_preset_category_filter_row == nullptr || studio_preset_pack_filter_combo == nullptr ||
      studio_preset_favorites_filter_button == nullptr ||
      studio_preset_library_list == nullptr || studio_preset_pack_header == nullptr ||
      studio_preset_library_card == nullptr ||
      studio_preset_favorite_button == nullptr || studio_preset_apply_card_button == nullptr ||
      studio_preset_manage_menu_button == nullptr || studio_preset_management_menu == nullptr ||
      studio_preset_rename_button == nullptr || studio_preset_move_button == nullptr ||
      studio_preset_delete_button == nullptr || studio_preset_export_field == nullptr ||
      studio_preset_export_button == nullptr || studio_preset_import_field == nullptr ||
      studio_preset_import_button == nullptr ||
      preset_library_group->property("title").toString() != QStringLiteral("Presets") ||
      !studio_preset_library_list->property("clip").toBool() ||
      studio_preset_library_list->property("height").toDouble() < 240.0 ||
      studio_preset_library_card->property("height").toDouble() < 96.0 ||
      !studio_preset_favorite_button->property("square").toBool() ||
      studio_preset_apply_card_button->property("text").toString() != QStringLiteral("Apply") ||
      studio_preset_manage_menu_button->property("text").toString() != QStringLiteral("Manage") ||
      studio_preset_delete_button->property("role").toString() != QStringLiteral("destructive")) {
    return fail("preset library should live in a dedicated Presets inspector tab with filters, large cards, and management controls");
  }
  edit_workspace->setProperty("studioPresetCategoryFilter", QStringLiteral("Landscape"));
  edit_workspace->setProperty("studioPresetPackFilter", QStringLiteral("Landscape Core"));
  edit_workspace->setProperty("studioPresetFavoritesOnly", false);
  for (auto iteration = 0; iteration < 4; ++iteration) {
    QCoreApplication::processEvents();
  }
  QVariant filtered_preset_names;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "studioPresetLibraryNames",
          Q_RETURN_ARG(QVariant, filtered_preset_names))) {
    return fail("preset browser filters should expose filtered library names to QML");
  }
  const auto filtered_preset_list = filtered_preset_names.toStringList();
  if (!filtered_preset_list.contains(QStringLiteral("Landscape Pop")) ||
      filtered_preset_list.contains(QStringLiteral("Clean Portrait"))) {
    return fail("preset browser filters should narrow the library by category and pack");
  }
  edit_workspace->setProperty("studioPresetCategoryFilter", QStringLiteral("All"));
  edit_workspace->setProperty("studioPresetPackFilter", QStringLiteral("All"));
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  for (auto iteration = 0; iteration < 6; ++iteration) {
    QCoreApplication::processEvents();
  }
  editor_preview.setCompareAdjustmentGroups(QStringList{});
  editor_preview.setExposure(0.42);
  editor_preview.setSaturation(17.0);
  QCoreApplication::processEvents();
  if (!adjust_inline_histogram_scope->property("visible").toBool() ||
      !adjust_inline_waveform_scope->property("visible").toBool()) {
    return fail("adjust inspector should keep histogram and waveform visible while editing exposure and color controls");
  }
  if (light_panel_before_after_button->property("iconName").toString() != QStringLiteral("compare") ||
      color_panel_before_after_button->property("iconName").toString() != QStringLiteral("compare") ||
      presence_panel_before_after_button->property("iconName").toString() != QStringLiteral("compare") ||
      !light_panel_before_after_button->property("square").toBool() ||
      light_panel_before_after_button->property("role").toString() != QStringLiteral("icon") ||
      !light_panel_before_after_button->property("visible").toBool() ||
      !color_panel_before_after_button->property("visible").toBool() ||
      !presence_panel_before_after_button->property("visible").toBool() ||
      light_panel_before_after_button->property("tooltip").toString().isEmpty()) {
    return fail("adjust inspector sections should expose compact before/after controls");
  }
  if (!QMetaObject::invokeMethod(light_panel_before_after_button, "click")) {
    return fail("light before/after section button should be clickable");
  }
  QCoreApplication::processEvents();
  if (!editor_preview.compareAdjustmentGroups().contains(QStringLiteral("tone")) ||
      editor_preview.compareAdjustmentGroups().contains(QStringLiteral("color")) ||
      !light_panel_before_after_button->property("active").toBool() ||
      std::abs(editor_preview.exposure() - 0.42) > 1e-6 ||
      std::abs(editor_preview.saturation() - 17.0) > 1e-6) {
    return fail("section before/after toggles should isolate compare groups without mutating adjustment values");
  }
  if (!QMetaObject::invokeMethod(color_panel_before_after_button, "click")) {
    return fail("color before/after section button should be clickable");
  }
  QCoreApplication::processEvents();
  if (!editor_preview.compareAdjustmentGroups().contains(QStringLiteral("tone")) ||
      !editor_preview.compareAdjustmentGroups().contains(QStringLiteral("color")) ||
      !color_panel_before_after_button->property("active").toBool()) {
    return fail("section before/after toggles should allow multiple explicit compare groups");
  }
  if (!QMetaObject::invokeMethod(light_panel_before_after_button, "click")) {
    return fail("light before/after section button should toggle off");
  }
  QCoreApplication::processEvents();
  if (editor_preview.compareAdjustmentGroups().contains(QStringLiteral("tone")) ||
      !editor_preview.compareAdjustmentGroups().contains(QStringLiteral("color")) ||
      light_panel_before_after_button->property("active").toBool()) {
    return fail("section before/after toggles should remove only their own compare group");
  }
  editor_preview.setCompareAdjustmentGroups(QStringList{});
  editor_preview.setExposure(0.0);
  editor_preview.setSaturation(0.0);
  QCoreApplication::processEvents();
  QVariant adjustment_filter_signature;
  edit_workspace->setProperty("editAdjustmentFilterText", QStringLiteral("dehaze"));
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "editAdjustmentFilterSignature",
          Q_RETURN_ARG(QVariant, adjustment_filter_signature)) ||
      adjustment_filter_signature.toString() != QStringLiteral("query:dehaze|count:1|groups:presence") ||
      edit_adjustment_filter_field->property("text").toString() != QStringLiteral("dehaze") ||
      !edit_adjustment_filter_status_label->property("visible").toBool() ||
      !edit_adjustment_filter_status_label->property("text").toString().contains(QStringLiteral("1 matching")) ||
      !clear_edit_adjustment_filter_button->property("visible").toBool() ||
      white_balance_group->property("visible").toBool() ||
      light_group->property("visible").toBool() ||
      tone_curve_group->property("visible").toBool() ||
      color_group->property("visible").toBool() ||
      !presence_group->property("visible").toBool() ||
      edit_workspace->property("editInspectorTab").toString() != QStringLiteral("adjust")) {
    return fail("adjust inspector filter should narrow visible adjustment groups without changing the active tab");
  }
  edit_workspace->setProperty("editAdjustmentFilterText", QStringLiteral("channel"));
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "editAdjustmentFilterSignature",
          Q_RETURN_ARG(QVariant, adjustment_filter_signature)) ||
      adjustment_filter_signature.toString() !=
          QStringLiteral("query:channel|count:1|groups:advanced-color") ||
      !advanced_color_group->property("visible").toBool() ||
      presence_group->property("visible").toBool()) {
    return fail("adjust inspector filter should find advanced color controls by contained capability names");
  }
  QVariant raw_availability_signature;
  QVariant disabled_capability_signature;
  edit_workspace->setProperty("editAdjustmentFilterText", QStringLiteral("raw"));
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "editAdjustmentFilterSignature",
          Q_RETURN_ARG(QVariant, adjustment_filter_signature)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "rawControlAvailabilitySignature",
          Q_RETURN_ARG(QVariant, raw_availability_signature)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "disabledCapabilityReasonSignature",
          Q_RETURN_ARG(QVariant, disabled_capability_signature)) ||
      editor_preview.rawControlsAvailable() ||
      adjustment_filter_signature.toString() != QStringLiteral("query:raw|count:0|groups:") ||
      raw_availability_signature.toString() !=
          QStringLiteral("available:false|rawGroups:0|groupVisible:false") ||
      !edit_adjustment_filter_status_label->property("text").toString().contains(QStringLiteral("RAW source")) ||
      !disabled_capability_signature.toString().contains(QStringLiteral("raw:disabled:RAW controls require")) ||
      !disabled_capability_signature.toString().contains(QStringLiteral("profile-camera-input:disabled:Camera input profiles require RAW data")) ||
      (editor_preview.aiSmartSelectionAvailable()
           ? !disabled_capability_signature.toString().contains(QStringLiteral("model-smart-selection:enabled"))
           : !disabled_capability_signature.toString().contains(
                  QStringLiteral("model-smart-selection:disabled:Model-backed smart selection is not active in this build; %1 is active")
                      .arg(editor_preview.smartSelectionRuntimeLabel()))) ||
      (editor_preview.aiInpaintAvailable()
           ? !disabled_capability_signature.toString().contains(QStringLiteral("model-inpaint:enabled"))
           : !disabled_capability_signature.toString().contains(
                  QStringLiteral("model-inpaint:disabled:LaMa Model Inpaint is not active in this build; retouch will use %1")
                      .arg(editor_preview.inpaintRuntimeLabel()))) ||
      raw_group->property("visible").toBool()) {
    return fail("non-RAW assets should hide RAW controls and explain disabled RAW/profile plus recoverable model capabilities");
  }
  if (studio_workspace_source.contains(QStringLiteral("SAM Smart Select is unavailable"))) {
    return fail("production UI should not expose SAM-ready wording when the SAM ONNX path is not implemented");
  }
  QVariant mask_runtime_trust_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "maskRuntimeTrustSignature",
          Q_RETURN_ARG(QVariant, mask_runtime_trust_signature)) ||
      !mask_runtime_trust_signature.toString().contains(
          QStringLiteral("selection:%1").arg(editor_preview.smartSelectionRuntimeLabel())) ||
      !mask_runtime_trust_signature.toString().contains(
          QStringLiteral("inpaint:%1").arg(editor_preview.inpaintRuntimeLabel())) ||
      !mask_runtime_trust_signature.toString().contains(QStringLiteral("diagnostics:root:")) ||
      !mask_runtime_trust_signature.toString().contains(QStringLiteral("|sam:")) ||
      !mask_runtime_trust_signature.toString().contains(QStringLiteral("|lama:")) ||
      !mask_runtime_trust_signature.toString().contains(QStringLiteral("|onnx:")) ||
      !mask_runtime_trust_signature.toString().contains(QStringLiteral("|vision:"))) {
    return fail("mask runtime trust signature should expose honest selection/inpaint labels and model diagnostics");
  }
  edit_workspace->setProperty("editAdjustmentFilterText", QString());
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "editAdjustmentFilterSignature",
          Q_RETURN_ARG(QVariant, adjustment_filter_signature)) ||
      adjustment_filter_signature.toString() !=
          QStringLiteral("query:|count:7|groups:white-balance,light,tone-curve,color,presence,detail,advanced-color") ||
      !white_balance_group->property("visible").toBool() ||
      !light_group->property("visible").toBool() ||
      !tone_curve_group->property("visible").toBool() ||
      !color_group->property("visible").toBool() ||
      !presence_group->property("visible").toBool() ||
      !adjust_detail_group->property("visible").toBool() ||
      !adjust_lens_group->property("visible").toBool() ||
      !advanced_color_group->property("visible").toBool()) {
    return fail("clearing the adjust inspector filter should restore the full adjustment stack");
  }
  for (auto* footer_child : edit_inspector_footer->findChildren<QObject*>()) {
    if (footer_child->property("text").toString() == QStringLiteral("Export")) {
      return fail("edit inspector footer should contain only edit-local actions, not export navigation");
    }
  }
  auto* initial_activity_indicator = find_named_item(root, QStringLiteral("floatingActivityIndicator"));
  auto* initial_edit_inspector = qobject_cast<QQuickItem*>(edit_inspector_panel);
  auto* initial_edit_footer = qobject_cast<QQuickItem*>(edit_inspector_footer);
  if (initial_activity_indicator == nullptr || initial_activity_indicator->width() <= 0.0 ||
      initial_activity_indicator->height() <= 0.0 || initial_edit_filmstrip == nullptr ||
      !item_visible(initial_edit_inspector) || !item_visible(initial_edit_footer)) {
    return fail("edit activity geometry test should expose indicator, filmstrip, inspector, and footer geometry");
  }
  const auto initial_indicator_rect = scene_rect(initial_activity_indicator);
  if ((item_visible(initial_edit_filmstrip) &&
       overlaps_with_tolerance(initial_indicator_rect, scene_rect(initial_edit_filmstrip), 1.0)) ||
      overlaps_with_tolerance(initial_indicator_rect, scene_rect(initial_edit_inspector), 1.0) ||
      overlaps_with_tolerance(initial_indicator_rect, scene_rect(initial_edit_footer), 1.0)) {
    return fail("activity indicator should not overlap the edit filmstrip, footer, or inspector controls");
  }
  if (light_basic_tone_group == nullptr || light_advanced_tone_group == nullptr ||
      light_levels_group != nullptr || light_zone_mapping_group == nullptr ||
      tone_curve_input_levels_group == nullptr || tone_curve_input_levels_strip == nullptr ||
      tone_curve_input_levels_histogram == nullptr || tone_curve_input_levels_handle == nullptr ||
      light_rolloff_detail_reset_button == nullptr || light_zone_mixer_reset_button == nullptr ||
      tone_curve_input_levels_reset_button == nullptr ||
      light_basic_tone_group->property("title").toString() != QStringLiteral("Basic Tone") ||
      light_advanced_tone_group->property("title").toString() != QStringLiteral("Roll-off & Detail") ||
      light_zone_mapping_group->property("title").toString() != QStringLiteral("Zone Mixer") ||
      tone_curve_input_levels_group->property("title").toString() != QStringLiteral("Input Levels") ||
      !light_advanced_tone_group->property("resetVisible").toBool() ||
      !light_zone_mapping_group->property("resetVisible").toBool() ||
      !tone_curve_input_levels_group->property("resetVisible").toBool() ||
      !light_basic_tone_group->property("expanded").toBool() ||
      light_advanced_tone_group->property("expanded").toBool() ||
      light_zone_mapping_group->property("expanded").toBool() ||
      tone_curve_input_levels_group->property("expanded").toBool() ||
      !(light_basic_tone_group->property("y").toDouble() <
        light_advanced_tone_group->property("y").toDouble() &&
        light_advanced_tone_group->property("y").toDouble() <
        light_zone_mapping_group->property("y").toDouble())) {
    return fail("light inspector should keep only tone controls while tone curve owns a collapsed input-level utility");
  }
  edit_workspace->setProperty("lightAdvancedToneExpanded", true);
  edit_workspace->setProperty("lightLevelsExpanded", true);
  edit_workspace->setProperty("lightZonesExpanded", true);
  QCoreApplication::processEvents();
  if (!light_advanced_tone_group->property("expanded").toBool() ||
      !tone_curve_input_levels_group->property("expanded").toBool() ||
      !light_zone_mapping_group->property("expanded").toBool()) {
    return fail("advanced tone, input levels, and zone groups should expand through explicit workspace state");
  }
  edit_workspace->setProperty("lightAdvancedToneExpanded", false);
  edit_workspace->setProperty("lightLevelsExpanded", false);
  edit_workspace->setProperty("lightZonesExpanded", false);
  QCoreApplication::processEvents();
  if (advanced_color_group == nullptr || color_mix_group == nullptr || channel_mixer_group == nullptr ||
      channel_mixer_red_sum_label == nullptr || channel_mixer_green_sum_label == nullptr ||
      channel_mixer_blue_sum_label == nullptr ||
      color_mix_active_preview == nullptr || color_mix_active_swatch == nullptr ||
      color_mix_active_label == nullptr || color_mix_active_values == nullptr ||
      color_mix_reset_channel_button == nullptr || color_mix_affected_range_strip == nullptr ||
      color_mix_affected_range_swatch == nullptr ||
      black_white_group == nullptr || color_replace_group == nullptr ||
      color_replace_swatch_preview == nullptr || color_replace_source_swatch == nullptr ||
      color_replace_target_swatch == nullptr || color_replace_summary_label == nullptr ||
      color_replace_selection_label == nullptr || color_replace_swap_button == nullptr ||
      color_replace_mask_preview_button == nullptr ||
      color_replace_auto_skin_button == nullptr || color_replace_reset_skin_button == nullptr ||
      color_replace_preserve_luminance_check == nullptr ||
      color_replace_preserve_saturation_check == nullptr ||
      skin_tone_group == nullptr ||
      harmony_group == nullptr || harmony_palette_preview == nullptr || harmony_anchor_swatch == nullptr ||
      harmony_affected_range_strip == nullptr || harmony_affected_range_swatch == nullptr ||
      harmony_warm_preset_button == nullptr || harmony_complement_preset_button == nullptr ||
      harmony_triad_preset_button == nullptr || harmony_base_hue_slider == nullptr ||
      harmony_amount_slider == nullptr || split_toning_group == nullptr ||
      hsl_channel_button_row == nullptr || hsl_reds_button == nullptr || hsl_magentas_button == nullptr ||
      advanced_color_group->property("title").toString() != QStringLiteral("Advanced Color") ||
      color_mix_group->property("title").toString() != QStringLiteral("Color Mix") ||
      !color_mix_affected_range_strip->property("visible").toBool() ||
      channel_mixer_group->property("title").toString() != QStringLiteral("Channel Mixer") ||
      channel_mixer_group->property("visible").toBool() ||
      channel_mixer_red_sum_label->property("text").toString() != QStringLiteral("Sum 100") ||
      channel_mixer_green_sum_label->property("text").toString() != QStringLiteral("Sum 100") ||
      channel_mixer_blue_sum_label->property("text").toString() != QStringLiteral("Sum 100") ||
      black_white_group->property("title").toString() != QStringLiteral("Black & White") ||
      color_replace_group->property("title").toString() != QStringLiteral("Color Replace") ||
      skin_tone_group->property("title").toString() != QStringLiteral("Skin Protection Lab") ||
      harmony_group->property("title").toString() != QStringLiteral("Harmony Lab") ||
      split_toning_group->property("title").toString() != QStringLiteral("Split Toning") ||
      harmony_group->property("visible").toBool() ||
      harmony_warm_preset_button->property("text").toString() != QStringLiteral("Warm") ||
      harmony_complement_preset_button->property("text").toString() != QStringLiteral("Comp") ||
      harmony_triad_preset_button->property("text").toString() != QStringLiteral("Triad") ||
      harmony_amount_slider->property("label").toString() != QStringLiteral("Amount") ||
      !color_mix_group->property("expanded").toBool() ||
      channel_mixer_group->property("expanded").toBool() ||
      black_white_group->property("expanded").toBool() ||
      color_replace_group->property("expanded").toBool() ||
      color_replace_mask_preview_button->property("active").toBool() ||
      color_replace_auto_skin_button->property("text").toString() != QStringLiteral("Auto Skin (Heuristic)") ||
      !color_replace_auto_skin_button->property("tooltip").toString().contains(QStringLiteral("color heuristic")) ||
      color_replace_reset_skin_button->property("text").toString() != QStringLiteral("Reset Skin") ||
      !color_replace_preserve_luminance_check->property("checked").toBool() ||
      !color_replace_preserve_saturation_check->property("checked").toBool() ||
      skin_tone_group->property("visible").toBool() ||
      skin_tone_group->property("expanded").toBool() ||
      harmony_group->property("expanded").toBool() ||
      !split_toning_group->property("expanded").toBool()) {
    return fail("advanced color inspector should keep color mix and color grading available while collapsing deeper specialist color tools by default");
  }
  QVariant hsl_reds_signature;
  QVariant hsl_magentas_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "hslChannelControlSignature",
          Q_RETURN_ARG(QVariant, hsl_reds_signature),
          Q_ARG(QVariant, QStringLiteral("Reds"))) ||
      hsl_reds_signature.toString() !=
          QStringLiteral("Reds|label:Red|tooltip:Reds color mix channel.") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "hslChannelControlSignature",
          Q_RETURN_ARG(QVariant, hsl_magentas_signature),
          Q_ARG(QVariant, QStringLiteral("Magentas"))) ||
      hsl_magentas_signature.toString() !=
          QStringLiteral("Magentas|label:Mag|tooltip:Magentas color mix channel.") ||
      hsl_reds_button->property("text").toString() != QStringLiteral("Red") ||
      hsl_magentas_button->property("text").toString() != QStringLiteral("Mag") ||
      hsl_reds_button->property("iconName").toString() != QStringLiteral("swatch") ||
      hsl_magentas_button->property("iconName").toString() != QStringLiteral("swatch") ||
      hsl_reds_button->property("tooltip").toString().isEmpty() ||
      hsl_magentas_button->property("tooltip").toString().isEmpty()) {
    return fail("HSL channel controls should use readable labels, swatch icons, and tooltips instead of ambiguous one-letter buttons");
  }
  editor_preview.setColorMixerModel(QStringLiteral("HSL"));
  editor_preview.setHslChannel(QStringLiteral("Blues"));
  editor_preview.setHueAdjustment(22.0);
  editor_preview.setSaturationAdjustment(-14.0);
  editor_preview.setLuminanceAdjustment(9.0);
  QCoreApplication::processEvents();
  QVariant color_mix_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "colorMixActiveChannelSignature",
          Q_RETURN_ARG(QVariant, color_mix_signature)) ||
      color_mix_signature.toString() != QStringLiteral("HSL|Blues|h:22|s:-14|l:9") ||
      color_mix_active_label->property("text").toString() != QStringLiteral("Blues / HSL") ||
      !color_mix_reset_channel_button->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(color_mix_reset_channel_button, "click")) {
    return fail("color mix should show the active channel and expose a reset action for selected HSL adjustments");
  }
  QCoreApplication::processEvents();
  if (
      std::abs(editor_preview.hueAdjustment()) > 1e-6 ||
      std::abs(editor_preview.saturationAdjustment()) > 1e-6 ||
      std::abs(editor_preview.luminanceAdjustment()) > 1e-6) {
    return fail("color mix reset should clear the selected HSL channel adjustments");
  }
  editor_preview.setColorReplacementSourceHue(0.10);
  editor_preview.setColorReplacementTargetHue(0.55);
  editor_preview.setColorReplacementRange(42.0);
  editor_preview.setColorReplacementSoftness(70.0);
  editor_preview.setColorReplacementStrength(80.0);
  QCoreApplication::processEvents();
  QVariant color_replace_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "colorReplaceSignature",
      Q_RETURN_ARG(QVariant, color_replace_signature)) ||
      color_replace_signature.toString() != QStringLiteral("source:36|target:198|range:42|softness:70|strength:80|preserveLuma:true|preserveSat:true") ||
      color_replace_summary_label->property("text").toString() != QStringLiteral("36° -> 198°") ||
      !QMetaObject::invokeMethod(color_replace_swap_button, "click")) {
    return fail("color replace should expose source/target swatches, readable selection state, and a hue swap action");
  }
  QCoreApplication::processEvents();
  if (std::abs(editor_preview.colorReplacementSourceHue() - 0.55) > 1e-6 ||
      std::abs(editor_preview.colorReplacementTargetHue() - 0.10) > 1e-6) {
    return fail("color replace swap should exchange source and target hues");
  }
  QVariant applied_harmony_preset;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "applyHarmonyPreset",
          Q_RETURN_ARG(QVariant, applied_harmony_preset),
          Q_ARG(QVariant, QStringLiteral("Cool Triad"))) ||
      !applied_harmony_preset.toBool() ||
      !editor_preview.colorHarmonyEnabled() ||
      editor_preview.colorHarmonyMode() != QStringLiteral("Triadic") ||
      std::abs(editor_preview.colorHarmonyBaseHue() - 0.56) > 1e-6 ||
      std::abs(editor_preview.colorHarmonyStrength() - 32.0) > 1e-6) {
    return fail("harmony lab should expose named palette presets that configure mode, base hue, and amount");
  }
  edit_workspace->setProperty("labColorToolsVisible", true);
  edit_workspace->setProperty("channelMixerExpanded", true);
  edit_workspace->setProperty("blackWhiteExpanded", true);
  edit_workspace->setProperty("colorReplaceExpanded", true);
  edit_workspace->setProperty("skinToneExpanded", true);
  edit_workspace->setProperty("harmonyExpanded", true);
  edit_workspace->setProperty("splitToningExpanded", true);
  QCoreApplication::processEvents();
  if (!channel_mixer_group->property("visible").toBool() ||
      !skin_tone_group->property("visible").toBool() ||
      !harmony_group->property("visible").toBool() ||
      !harmony_affected_range_strip->property("visible").toBool() ||
      !channel_mixer_group->property("expanded").toBool() ||
      !black_white_group->property("expanded").toBool() ||
      !color_replace_group->property("expanded").toBool() ||
      !skin_tone_group->property("expanded").toBool() ||
      !harmony_group->property("expanded").toBool() ||
      !split_toning_group->property("expanded").toBool()) {
    return fail("specialist color tools should become visible in Lab and expand through explicit workspace state");
  }
  edit_workspace->setProperty("labColorToolsVisible", false);
  edit_workspace->setProperty("channelMixerExpanded", false);
  edit_workspace->setProperty("blackWhiteExpanded", false);
  edit_workspace->setProperty("colorReplaceExpanded", false);
  edit_workspace->setProperty("skinToneExpanded", false);
  edit_workspace->setProperty("harmonyExpanded", false);
  edit_workspace->setProperty("splitToningExpanded", false);
  QCoreApplication::processEvents();
  auto* tone_curve_editor = edit_workspace->findChild<QObject*>(QStringLiteral("studioToneCurveEditor"));
  auto* tone_curve_delete_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("studioToneCurveEditorDeletePointButton"));
  auto* tone_curve_reset_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("studioToneCurveEditorResetCurveButton"));
  auto* tone_curve_mode_badge =
      edit_workspace->findChild<QObject*>(QStringLiteral("studioToneCurveEditorChannelModeBadge"));
  if (tone_curve_editor == nullptr || tone_curve_delete_button == nullptr ||
      tone_curve_reset_button == nullptr || tone_curve_mode_badge == nullptr) {
    return fail("tone curve editor should expose visible channel, delete, and reset controls");
  }
  editor_preview.setCurveChannel(QStringLiteral("RGB"));
  editor_preview.setCurveShadows(18.0);
  QCoreApplication::processEvents();
  if (editor_preview.activeCurveMode() != QStringLiteral("Parametric") ||
      tone_curve_editor->property("channelModeLabel").toString() != QStringLiteral("RGB Parametric")) {
    return fail("tone curve editor should identify RGB parametric mode before point editing takes over");
  }
  editor_preview.setActiveCurvePoints(QVariantList{
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 0.48}, {QStringLiteral("y"), 0.56}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
  });
  QCoreApplication::processEvents();
  if (editor_preview.activeCurveMode() != QStringLiteral("Point") ||
      tone_curve_editor->property("channelModeLabel").toString() != QStringLiteral("RGB Point") ||
      std::abs(editor_preview.curveShadows()) > 1e-6) {
    return fail("tone curve editor should identify point mode after drawing a curve and clear RGB parametric controls");
  }
  editor_preview.setCurveChannel(QStringLiteral("Red"));
  editor_preview.setActiveCurvePoints(QVariantList{
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 0.42}, {QStringLiteral("y"), 0.62}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
  });
  QCoreApplication::processEvents();
  QVariant selected_curve_index;
  if (tone_curve_editor->property("channelModeLabel").toString() != QStringLiteral("Red Point") ||
      tone_curve_editor->property("pointCount").toInt() != 3 ||
      tone_curve_editor->property("histogramPrimaryFillOpacity").toDouble() > 0.10 ||
      tone_curve_editor->property("histogramPrimaryStrokeOpacity").toDouble() > 0.28 ||
      tone_curve_editor->property("histogramRgbFillOpacity").toDouble() > 0.04 ||
      tone_curve_editor->property("histogramRgbStrokeOpacity").toDouble() > 0.12 ||
      !QMetaObject::invokeMethod(
          tone_curve_editor,
          "testSelectPoint",
          Q_RETURN_ARG(QVariant, selected_curve_index),
          Q_ARG(QVariant, QVariant(1))) ||
      selected_curve_index.toInt() != 1 ||
      !tone_curve_editor->property("selectedPointDeletable").toBool() ||
      !tone_curve_delete_button->property("enabled").toBool()) {
    return fail("tone curve editor should make channel mode, selected point deletion, and quiet histogram opacity testable");
  }
  QVariant deleted_curve_point;
  if (!QMetaObject::invokeMethod(
          tone_curve_editor,
          "testDeleteSelectedPoint",
          Q_RETURN_ARG(QVariant, deleted_curve_point)) ||
      !deleted_curve_point.toBool()) {
    return fail("tone curve editor should delete the selected interior point through an explicit control path");
  }
  QCoreApplication::processEvents();
  if (editor_preview.activeCurvePoints().size() != 2) {
    return fail("tone curve selected-point delete should update the active channel curve");
  }
  QVariant reset_curve;
  editor_preview.setActiveCurvePoints(QVariantList{
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.16}},
      QVariantMap{{QStringLiteral("x"), 0.55}, {QStringLiteral("y"), 0.44}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 0.92}},
  });
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          tone_curve_editor,
          "testResetActiveCurve",
          Q_RETURN_ARG(QVariant, reset_curve)) ||
      !reset_curve.toBool()) {
    return fail("tone curve editor should reset the active curve through an explicit control path");
  }
  QCoreApplication::processEvents();
  if (editor_preview.activeCurvePoints().size() != 2 ||
      !tone_curve_reset_button->property("enabled").toBool()) {
    return fail("tone curve reset should restore the active channel to the default curve");
  }
  QVariant previewed_curve_preset;
  if (!QMetaObject::invokeMethod(
          tone_curve_editor,
          "testPreviewPreset",
          Q_RETURN_ARG(QVariant, previewed_curve_preset),
          Q_ARG(QVariant, QVariant(QStringLiteral("Filmic S")))) ||
      !previewed_curve_preset.toBool() ||
      tone_curve_editor->property("previewPointsModel").toList().size() < 5 ||
      editor_preview.activeCurvePoints().size() != 2) {
    return fail("tone curve preset preview should show preset points without mutating the active curve");
  }
  QVariant cleared_curve_preview;
  if (!QMetaObject::invokeMethod(
          tone_curve_editor,
          "testClearPresetPreview",
          Q_RETURN_ARG(QVariant, cleared_curve_preview)) ||
      !cleared_curve_preview.toBool() ||
      !tone_curve_editor->property("previewPointsModel").toList().isEmpty() ||
      editor_preview.activeCurvePoints().size() != 2) {
    return fail("tone curve preset preview should be reversible without changing the recipe");
  }
  auto* brightness_slider = edit_workspace->findChild<QObject*>(QStringLiteral("brightnessSlider"));
  auto* tint_slider = edit_workspace->findChild<QObject*>(QStringLiteral("tintSlider"));
  if (brightness_slider == nullptr || tint_slider == nullptr ||
      std::abs(brightness_slider->property("defaultPosition").toDouble() - 0.5) > 1e-6 ||
      std::abs(tint_slider->property("defaultPosition").toDouble() - 0.5) > 1e-6) {
    return fail("signed adjustment sliders should expose a centered neutral/default position");
  }
  editor_preview.setBrightness(40.0);
  QCoreApplication::processEvents();
  if (brightness_slider->property("valueText").toString() != QStringLiteral("+40%") ||
      brightness_slider->property("normalizedValue").toDouble() <= 0.5 ||
      std::abs(brightness_slider->property("fillStartPosition").toDouble() - 0.5) > 1e-6 ||
      brightness_slider->property("fillEndPosition").toDouble() <= 0.5) {
    return fail("positive signed adjustments should show a plus percent label and fill right from neutral");
  }
  editor_preview.setBrightness(-40.0);
  QCoreApplication::processEvents();
  if (brightness_slider->property("valueText").toString() != QStringLiteral("-40%") ||
      brightness_slider->property("normalizedValue").toDouble() >= 0.5 ||
      brightness_slider->property("fillStartPosition").toDouble() >= 0.5 ||
      std::abs(brightness_slider->property("fillEndPosition").toDouble() - 0.5) > 1e-6) {
    return fail("negative signed adjustments should show a minus percent label and fill left from neutral");
  }
  editor_preview.setBrightness(0.0);
  editor_preview.setTint(25.0);
  QCoreApplication::processEvents();
  if (tint_slider->property("valueText").toString() != QStringLiteral("+25%") ||
      tint_slider->property("trackColorCount").toInt() < 2) {
    return fail("color signed adjustments should use signed percent text without losing directional track colors");
  }
  editor_preview.setTint(0.0);
  QCoreApplication::processEvents();
  auto* exposure_slider = edit_workspace->findChild<QObject*>(QStringLiteral("exposureSlider"));
  auto* exposure_value_field = edit_workspace->findChild<QObject*>(QStringLiteral("exposureSliderValueField"));
  auto* exposure_modified_marker = edit_workspace->findChild<QObject*>(QStringLiteral("exposureSliderModifiedMarker"));
  auto* temperature_slider = edit_workspace->findChild<QObject*>(QStringLiteral("temperatureSlider"));
  auto* temperature_value_field = edit_workspace->findChild<QObject*>(QStringLiteral("temperatureSliderValueField"));
  auto* channel_mixer_red_slider = edit_workspace->findChild<QObject*>(QStringLiteral("channelMixerRedFromRedSlider"));
  auto* color_replacement_hue_slider =
      edit_workspace->findChild<QObject*>(QStringLiteral("colorReplacementTargetHueSlider"));
  auto* selected_tone_hue_slider =
      edit_workspace->findChild<QObject*>(QStringLiteral("selectedToneWheelHueSlider"));
  auto* split_toning_shadows_wheel =
      find_named_object(edit_workspace, QStringLiteral("splitToningShadowsWheel"));
  auto* split_toning_midtones_wheel =
      find_named_object(edit_workspace, QStringLiteral("splitToningMidtonesWheel"));
  auto* split_toning_highlights_wheel =
      find_named_object(edit_workspace, QStringLiteral("splitToningHighlightsWheel"));
  auto* split_toning_focused_wheel =
      find_named_object(edit_workspace, QStringLiteral("splitToningFocusedWheel"));
  auto* grading_amount_slider = edit_workspace->findChild<QObject*>(QStringLiteral("colorGradingAmountSlider"));
  auto* grading_balance_slider = edit_workspace->findChild<QObject*>(QStringLiteral("colorGradingBalanceSlider"));
  auto* selected_tone_saturation_slider =
      edit_workspace->findChild<QObject*>(QStringLiteral("selectedToneWheelSaturationSlider"));
  auto* selected_tone_luminance_slider =
      edit_workspace->findChild<QObject*>(QStringLiteral("selectedToneWheelLuminanceSlider"));
  auto* liquify_radius_slider = edit_workspace->findChild<QObject*>(QStringLiteral("liquifyRadiusSlider"));
  QVariant committed_input_value;
  if (exposure_slider == nullptr || exposure_value_field == nullptr || exposure_modified_marker == nullptr ||
      temperature_slider == nullptr ||
      temperature_value_field == nullptr || !exposure_value_field->property("enabled").toBool() ||
      !temperature_value_field->property("enabled").toBool()) {
    return fail("precise exposure entry should expose enabled typed EV fields");
  }
  if (!QMetaObject::invokeMethod(
          exposure_slider,
          "testCommitInputText",
          Q_RETURN_ARG(QVariant, committed_input_value),
          Q_ARG(QVariant, QStringLiteral("+1.37 EV")))) {
    return fail("precise exposure entry should accept typed EV values through the slider value field");
  }
  QCoreApplication::processEvents();
  if (
      std::abs(editor_preview.exposure() - 1.37) > 1e-6 ||
      exposure_slider->property("valueText").toString() != QStringLiteral("+1.37 EV") ||
      !exposure_slider->property("modified").toBool() ||
      !exposure_modified_marker->property("visible").toBool()) {
    return fail("precise exposure entry should accept typed EV values through the slider value field");
  }
  if (!QMetaObject::invokeMethod(
          temperature_slider,
          "testCommitInputText",
          Q_RETURN_ARG(QVariant, committed_input_value),
          Q_ARG(QVariant, QStringLiteral("6500K"))) ||
      std::abs(editor_preview.temperature() - ((6500.0 - 5500.0) / (12000.0 - 5500.0) * 100.0)) > 1e-6 ||
      temperature_slider->property("valueText").toString() != QStringLiteral("6500K")) {
    return fail("white balance temperature entry should accept typed Kelvin values through the slider value field");
  }
  if (color_replacement_hue_slider == nullptr || selected_tone_hue_slider == nullptr ||
      split_toning_shadows_wheel == nullptr || split_toning_midtones_wheel == nullptr ||
      split_toning_highlights_wheel == nullptr ||
      liquify_radius_slider == nullptr ||
      !QMetaObject::invokeMethod(
          color_replacement_hue_slider,
          "testCommitInputText",
          Q_RETURN_ARG(QVariant, committed_input_value),
          Q_ARG(QVariant, QStringLiteral("180°"))) ||
      std::abs(editor_preview.colorReplacementTargetHue() - 0.5) > 1e-6 ||
      color_replacement_hue_slider->property("valueText").toString() != QStringLiteral("180°") ||
      color_replacement_hue_slider->property("valueInputMode").toString() !=
          QStringLiteral("degreesNormalized")) {
    return fail("color hue sliders should accept typed degree values instead of raw normalized values");
  }
  if (split_toning_focused_wheel == nullptr || grading_amount_slider == nullptr ||
      grading_balance_slider == nullptr || selected_tone_saturation_slider == nullptr ||
      selected_tone_luminance_slider == nullptr ||
      split_toning_focused_wheel->property("wheelSize").toDouble() < 140.0 ||
      grading_amount_slider->property("controlHeight").toInt() < 40 ||
      grading_balance_slider->property("controlHeight").toInt() < 40 ||
      selected_tone_hue_slider->property("controlHeight").toInt() < 40 ||
      selected_tone_saturation_slider->property("controlHeight").toInt() < 40 ||
      selected_tone_luminance_slider->property("controlHeight").toInt() < 40 ||
      grading_amount_slider->property("valueText").toString() != QStringLiteral("100") ||
      grading_balance_slider->property("valueText").toString() != QStringLiteral("0%")) {
    return fail("color grading should expose a large focused wheel plus 40px Amount, Balance, Hue, Saturation, and Luminance sliders");
  }
  if (split_toning_shadows_wheel->property("minimumUsefulWidth").toInt() > 96 ||
      split_toning_midtones_wheel->property("minimumUsefulWidth").toInt() > 96 ||
      split_toning_highlights_wheel->property("minimumUsefulWidth").toInt() > 96) {
    return fail("split-toning preview wheels should stay compact enough to fit three-up in the inspector");
  }
  if (!QMetaObject::invokeMethod(
          selected_tone_hue_slider,
          "testCommitInputText",
          Q_RETURN_ARG(QVariant, committed_input_value),
          Q_ARG(QVariant, QStringLiteral("90°"))) ||
      std::abs(editor_preview.gradingMidtonesHue() - 0.25) > 1e-6 ||
      selected_tone_hue_slider->property("valueText").toString() != QStringLiteral("90°")) {
    return fail("color grading hue slider typed values should match the displayed degree unit");
  }
  if (!QMetaObject::invokeMethod(
          liquify_radius_slider,
          "testCommitInputText",
          Q_RETURN_ARG(QVariant, committed_input_value),
          Q_ARG(QVariant, QStringLiteral("33%"))) ||
      std::abs(editor_preview.liquifyRadius() - 0.33) > 1e-6 ||
      liquify_radius_slider->property("valueText").toString() != QStringLiteral("33%") ||
      liquify_radius_slider->property("valueInputMode").toString() != QStringLiteral("percentNormalized")) {
    return fail("normalized percent sliders should accept typed percentages instead of raw 0..1 values");
  }
  if (!QMetaObject::invokeMethod(
          exposure_slider,
          "testCommitInputText",
          Q_RETURN_ARG(QVariant, committed_input_value),
          Q_ARG(QVariant, QStringLiteral("+0.42 EV"))) ||
      std::abs(editor_preview.exposure() - 0.42) > 1e-6) {
    return fail("typed exposure input should commit the expected recipe value before slider equivalence proof");
  }
  const auto exposure_recipe_from_typed_input = editor_preview.currentRecipeJson();
  editor_preview.setExposure(0.0);
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          exposure_slider,
          "valueDragged",
          Q_ARG(double, 0.42)) ||
      std::abs(editor_preview.exposure() - 0.42) > 1e-6 ||
      editor_preview.currentRecipeJson() != exposure_recipe_from_typed_input) {
    return fail("typed numeric input and dragged slider input should produce identical recipe state");
  }
  editor_preview.setExposure(0.0);
  editor_preview.setTemperature(0.0);
  editor_preview.setColorReplacementTargetHue(0.33);
  editor_preview.setGradingMidtonesHue(0.0);
  editor_preview.setLiquifyRadius(0.25);
  QCoreApplication::processEvents();
  editor_preview.setExposure(0.85);
  editor_preview.setChannelMixerRedFromRed(42.0);
  QCoreApplication::processEvents();
  if (exposure_slider == nullptr || channel_mixer_red_slider == nullptr ||
      exposure_slider->property("valueText").toString() != QStringLiteral("+0.85 EV") ||
      !exposure_slider->property("modified").toBool() ||
      !channel_mixer_red_slider->property("modified").toBool() ||
      !QMetaObject::invokeMethod(exposure_slider, "resetRequested") ||
      !QMetaObject::invokeMethod(channel_mixer_red_slider, "resetRequested")) {
    return fail("adjustment sliders should expose a testable double-click reset signal");
  }
  QCoreApplication::processEvents();
  if (std::abs(editor_preview.exposure()) > 1e-6 ||
      std::abs(editor_preview.channelMixerRedFromRed() - 100.0) > 1e-6 ||
      exposure_slider->property("modified").toBool() ||
      exposure_modified_marker->property("visible").toBool() ||
      channel_mixer_red_slider->property("modified").toBool()) {
    return fail("double-click reset should restore each adjustment slider to its declared default value");
  }
  edit_workspace->setProperty("editTool", QStringLiteral("adjust"));
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  for (auto iteration = 0; iteration < 6; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* crop_inspector_group = find_named_object(edit_workspace, QStringLiteral("cropInspectorGroup"));
  auto* crop_box_group = find_named_object(edit_workspace, QStringLiteral("cropBoxGroup"));
  auto* crop_guide_aspect_group = find_named_object(edit_workspace, QStringLiteral("cropGuideAspectGroup"));
  auto* crop_transform_group = find_named_object(edit_workspace, QStringLiteral("cropTransformGroup"));
  auto* crop_liquify_group = find_named_object(edit_workspace, QStringLiteral("cropLiquifyGroup"));
  auto* detail_inspector_group = find_named_object(edit_workspace, QStringLiteral("detailInspectorGroup"));
  auto* lens_inspector_group = find_named_object(edit_workspace, QStringLiteral("lensInspectorGroup"));
  if (crop_inspector_group == nullptr || crop_box_group == nullptr || crop_guide_aspect_group == nullptr ||
      crop_transform_group == nullptr || crop_liquify_group == nullptr || detail_inspector_group == nullptr ||
      lens_inspector_group == nullptr ||
      crop_inspector_group->property("visible").toBool() ||
      !detail_inspector_group->property("visible").toBool() ||
      !lens_inspector_group->property("visible").toBool()) {
    return fail("crop controls should stay hidden until Crop owns the current edit mode, while Detail and Lens live in Adjust");
  }
  if (detail_inspect_button == nullptr || detail_loupe_button == nullptr ||
      detail_mask_preview_button == nullptr || detail_sharpening_group == nullptr ||
      detail_noise_reduction_group == nullptr || detail_before_after_button == nullptr) {
    return fail("detail inspector should expose inspection, sharpen, masking, and noise reduction controls");
  }
  auto* detail_edit_image_viewport = find_named_object(edit_workspace, QStringLiteral("editImageViewport"));
  QVariant detail_inspection_started;
  if (detail_edit_image_viewport == nullptr ||
      !QMetaObject::invokeMethod(
          detail_edit_image_viewport,
          "inspectDetailAtOneToOne",
          Q_RETURN_ARG(QVariant, detail_inspection_started)) ||
      !detail_inspection_started.toBool() ||
      detail_edit_image_viewport->property("zoom").toDouble() <= 1.01 ||
      !edit_workspace->property("loupeEnabled").toBool() ||
      !edit_workspace->property("focusPeakingOverlayEnabled").toBool()) {
    return fail("detail inspection should switch the edit viewport into 1:1 loupe/peaking review");
  }
  edit_workspace->setProperty("loupeEnabled", false);
  edit_workspace->setProperty("focusPeakingOverlayEnabled", false);
  detail_edit_image_viewport->setProperty("zoom", 1.0);
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activateEditTool",
          Q_ARG(QVariant, QVariant(QStringLiteral("crop"))))) {
    return fail("edit workspace should expose crop inspector activation");
  }
  QCoreApplication::processEvents();
  if (!crop_inspector_group->property("visible").toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("crop") ||
      edit_workspace->property("editInspectorTab").toString() != QStringLiteral("crop") ||
      edit_inspector_panel->property("title").toString() != QStringLiteral("Crop") ||
      !crop_inspector_tab->property("visible").toBool() ||
      !crop_inspector_tab->property("active").toBool() ||
      detail_inspector_tab->property("active").toBool() ||
      detail_inspector_group->property("visible").toBool() ||
      lens_inspector_group->property("visible").toBool() ||
      crop_box_group->property("title").toString() != QStringLiteral("Crop Box") ||
      crop_guide_aspect_group->property("title").toString() != QStringLiteral("Guides & Aspect") ||
      crop_transform_group->property("title").toString() != QStringLiteral("Transform & Upright") ||
      crop_liquify_group->property("title").toString() != QStringLiteral("Liquify") ||
      !crop_box_group->property("expanded").toBool() ||
      !crop_guide_aspect_group->property("expanded").toBool() ||
      crop_transform_group->property("expanded").toBool() ||
      crop_liquify_group->property("expanded").toBool() ||
      crop_liquify_group->property("visible").toBool()) {
    return fail("crop inspector should be a dedicated mode-owned tab with only crop controls visible");
  }
  edit_workspace->setProperty("cropTransformExpanded", true);
  QCoreApplication::processEvents();
  if (!crop_transform_group->property("expanded").toBool() ||
      crop_liquify_group->property("visible").toBool()) {
    return fail("crop inspector should expand real transform controls while hiding unavailable liquify controls");
  }
  edit_workspace->setProperty("cropTransformExpanded", false);
  edit_workspace->setProperty("cropLiquifyExpanded", false);
  QCoreApplication::processEvents();
  auto* crop_guide_mode_combo = find_named_object(edit_workspace, QStringLiteral("cropGuideModeComboBox"));
  QVariant crop_guide_signature;
  QVariant crop_guide_set_result;
  if (crop_guide_mode_combo == nullptr ||
      crop_guide_mode_combo->property("count").toInt() != 6 ||
      edit_workspace->findChild<QObject*>(QStringLiteral("cropGuideOffButton")) != nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("cropGuideThirdsButton")) != nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("cropGuideGoldenButton")) != nullptr ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "cropGuideControlSignature",
          Q_RETURN_ARG(QVariant, crop_guide_signature)) ||
      !crop_guide_signature.toString().contains(QStringLiteral("Off,Thirds,Center,Diagonal,Golden Ratio,Golden Spiral")) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setCropGuideMode",
          Q_RETURN_ARG(QVariant, crop_guide_set_result),
          Q_ARG(QVariant, QStringLiteral("Golden Spiral"))) ||
      !crop_guide_set_result.toBool() ||
      crop_guide_mode_combo->property("currentText").toString() != QStringLiteral("Golden Spiral")) {
    return fail("crop guide choices should live in one compact selector without six persistent guide buttons");
  }

  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setCropToolMode",
          Q_ARG(QVariant, QVariant(QStringLiteral("transform"))))) {
    return fail("edit workspace should expose crop submode switching");
  }
  QCoreApplication::processEvents();
  auto* crop_tool_hud = edit_workspace->findChild<QObject*>(QStringLiteral("cropToolHud"));
  auto* crop_tool_header_label = edit_workspace->findChild<QObject*>(QStringLiteral("cropToolHeaderLabel"));
  auto* crop_tool_aspect_label = edit_workspace->findChild<QObject*>(QStringLiteral("cropToolAspectLabel"));
  auto* crop_active_canvas_border = edit_workspace->findChild<QObject*>(QStringLiteral("cropActiveCanvasBorder"));
  auto* crop_box_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("cropBoxOverlay"));
  auto* crop_horizon_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("cropHorizonOverlay"));
  auto* crop_transform_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("cropTransformOverlay"));
  auto* edit_zoom_toolbar_for_crop = find_named_object(edit_workspace, QStringLiteral("editZoomToolbar"));
  auto* edit_action_toolbar_for_crop = find_named_object(edit_workspace, QStringLiteral("editActionToolbar"));
  auto* edit_context_panel_for_crop = find_named_object(edit_workspace, QStringLiteral("editContextPanel"));
  auto* edit_filmstrip_for_crop = find_named_object(edit_workspace, QStringLiteral("editFilmstrip"));
  editor_preview.setCropPreset(QStringLiteral("4x5"));
  QCoreApplication::processEvents();
  if (crop_tool_hud == nullptr || crop_tool_header_label == nullptr || crop_tool_aspect_label == nullptr ||
      crop_active_canvas_border == nullptr ||
      crop_box_overlay == nullptr || crop_horizon_overlay == nullptr || crop_transform_overlay == nullptr ||
      edit_zoom_toolbar_for_crop == nullptr || edit_action_toolbar_for_crop == nullptr ||
      edit_context_panel_for_crop == nullptr || edit_filmstrip_for_crop == nullptr ||
      !crop_tool_hud->property("visible").toBool() || !crop_active_canvas_border->property("visible").toBool() ||
      !edit_workspace->property("activeCanvasToolChromeReduced").toBool() ||
      edit_workspace->property("editContextPanelActive").toBool() ||
      edit_context_panel_for_crop->property("visible").toBool() ||
      edit_filmstrip_for_crop->property("visible").toBool() ||
      edit_zoom_toolbar_for_crop->property("visible").toBool() ||
      edit_action_toolbar_for_crop->property("visible").toBool() ||
      edit_workspace->property("cropToolMode").toString() !=
          QStringLiteral("transform") ||
      crop_tool_header_label->property("text").toString() != QStringLiteral("CROP / TRANSFORM") ||
      crop_tool_aspect_label->property("text").toString() != QStringLiteral("Aspect 4x5") ||
      editor_preview.cropEditing() || crop_box_overlay->property("active").toBool() ||
      !crop_transform_overlay->property("active").toBool()) {
    return fail("crop transform mode should own the canvas HUD, show aspect, and reduce persistent edit chrome");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setCropToolMode",
          Q_ARG(QVariant, QVariant(QStringLiteral("horizon"))))) {
    return fail("edit workspace should expose horizon straighten submode switching");
  }
  QCoreApplication::processEvents();
  QVariant horizon_applied;
  constexpr auto kPi = 3.14159265358979323846;
  const auto expected_horizon_angle = std::atan2(0.2, 0.8) * 180.0 / kPi;
  if (edit_workspace->property("cropToolMode").toString() != QStringLiteral("horizon") ||
      crop_tool_header_label->property("text").toString() != QStringLiteral("CROP / HORIZON") ||
      editor_preview.cropEditing() || crop_box_overlay->property("active").toBool() ||
      crop_transform_overlay->property("active").toBool() ||
      !crop_horizon_overlay->property("active").toBool() ||
      !QMetaObject::invokeMethod(
          crop_horizon_overlay,
          "applyNormalizedGuide",
          Q_RETURN_ARG(QVariant, horizon_applied),
          Q_ARG(QVariant, 0.1),
          Q_ARG(QVariant, 0.3),
          Q_ARG(QVariant, 0.9),
          Q_ARG(QVariant, 0.5)) ||
      !horizon_applied.toBool() || std::abs(editor_preview.transformRotation() + expected_horizon_angle) > 1e-4) {
    std::cerr << "Horizon state: mode=" << edit_workspace->property("cropToolMode").toString().toStdString()
              << " cropEditing=" << editor_preview.cropEditing()
              << " cropActive=" << crop_box_overlay->property("active").toBool()
              << " transformActive=" << crop_transform_overlay->property("active").toBool()
              << " horizonActive=" << crop_horizon_overlay->property("active").toBool()
              << " applied=" << horizon_applied.toBool()
              << " rotation=" << editor_preview.transformRotation()
              << " expected=" << -expected_horizon_angle << '\n';
    return fail("crop horizon mode should expose a direct draggable guide that straightens from endpoints");
  }
  editor_preview.undo();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setCropToolMode",
          Q_ARG(QVariant, QVariant(QStringLiteral("crop"))))) {
    return fail("edit workspace should switch back to crop-box mode");
  }
  QCoreApplication::processEvents();
  if (edit_workspace->property("cropToolMode").toString() != QStringLiteral("crop") ||
      crop_tool_header_label->property("text").toString() != QStringLiteral("CROP / BOX") ||
      !editor_preview.cropEditing() || !crop_box_overlay->property("active").toBool() ||
      crop_transform_overlay->property("active").toBool()) {
    return fail("crop-box mode should reactivate direct crop editing and hide transform handles");
  }
  crop_box_overlay->setProperty("imageX", 0.0);
  crop_box_overlay->setProperty("imageY", 0.0);
  crop_box_overlay->setProperty("imageWidth", 200.0);
  crop_box_overlay->setProperty("imageHeight", 100.0);
  crop_box_overlay->setProperty("cropX", 0.25);
  crop_box_overlay->setProperty("cropY", 0.2);
  crop_box_overlay->setProperty("cropWidth", 0.5);
  crop_box_overlay->setProperty("cropHeight", 0.5);
  QCoreApplication::processEvents();
  QVariant near_corner_hit_mode;
  QVariant center_hit_mode;
  if (crop_box_overlay->property("handleHitSize").toDouble() < 28.0 ||
      crop_box_overlay->property("handleVisualSize").toDouble() > 16.0 ||
      !QMetaObject::invokeMethod(
          crop_box_overlay,
          "testHitMode",
          Q_RETURN_ARG(QVariant, near_corner_hit_mode),
          Q_ARG(QVariant, 39.0),
          Q_ARG(QVariant, 9.0)) ||
      near_corner_hit_mode.toString() != QStringLiteral("topLeft") ||
      !QMetaObject::invokeMethod(
          crop_box_overlay,
          "testHitMode",
          Q_RETURN_ARG(QVariant, center_hit_mode),
          Q_ARG(QVariant, 100.0),
          Q_ARG(QVariant, 45.0)) ||
      center_hit_mode.toString() != QStringLiteral("move")) {
    return fail("crop-box overlay should expose forgiving handle hit targets without turning the whole crop into a handle");
  }
  const QStringList guide_modes{
      QStringLiteral("Off"),
      QStringLiteral("Thirds"),
      QStringLiteral("Center"),
      QStringLiteral("Diagonal"),
      QStringLiteral("Golden Ratio"),
      QStringLiteral("Golden Spiral"),
  };
  if (crop_box_overlay->property("guideLineOpacity").toDouble() > 0.45 ||
      crop_box_overlay->property("guideSpiralOpacity").toDouble() > 0.48) {
    return fail("crop guide overlays should stay visually quiet enough not to overpower the image");
  }
  for (const auto& guide_mode : guide_modes) {
    edit_workspace->setProperty("cropGuideMode", guide_mode);
    QCoreApplication::processEvents();
    if (crop_box_overlay->property("guideMode").toString() != guide_mode) {
      return fail("crop guide selector should bind every guide mode to the on-canvas overlay");
    }
  }
  edit_workspace->setProperty("editTool", QStringLiteral("adjust"));
  editor_preview.setCropEditing(false);
  QCoreApplication::processEvents();

  const auto initial_mask_count = editor_preview.maskCount();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskCreationOperation",
          Q_ARG(QVariant, QVariant(QStringLiteral("intersect")))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "createMaskSource",
          Q_ARG(QVariant, QVariant(QStringLiteral("linear"))))) {
    return fail("edit workspace should expose testable mask creation controls");
  }
  QCoreApplication::processEvents();
  auto* mask_creation_toolbar = edit_workspace->findChild<QObject*>(QStringLiteral("maskCreationToolbar"));
  auto* mask_tool_rail = edit_workspace->findChild<QObject*>(QStringLiteral("maskToolRail"));
  auto* mask_tool_rail_toggle_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskToolRailToggleButton"));
  auto* mask_tool_rail_section_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskToolRailSectionLabel"));
  auto* mask_creation_vocabulary_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskCreationVocabularyLabel"));
  auto* mask_source_object_button = edit_workspace->findChild<QObject*>(QStringLiteral("maskSourceObjectButton"));
  auto* mask_source_subject_button = edit_workspace->findChild<QObject*>(QStringLiteral("maskSourceSubjectButton"));
  auto* mask_source_sky_button = edit_workspace->findChild<QObject*>(QStringLiteral("maskSourceSkyButton"));
  auto* mask_source_background_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskSourceBackgroundButton"));
  auto* mask_source_brush_add_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskSourceBrushAddButton"));
  auto* mask_source_brush_erase_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskSourceBrushEraseButton"));
  auto* mask_source_ellipse_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskSourceEllipseButton"));
  auto* mask_source_rectangle_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskSourceRectangleButton"));
  auto* mask_add_button = edit_workspace->findChild<QObject*>(QStringLiteral("maskCombineAddButton"));
  auto* mask_subtract_button = edit_workspace->findChild<QObject*>(QStringLiteral("maskCombineSubtractButton"));
  auto* mask_intersect_button = edit_workspace->findChild<QObject*>(QStringLiteral("maskCombineIntersectButton"));
  auto* mask_replace_button = edit_workspace->findChild<QObject*>(QStringLiteral("maskCombineReplaceButton"));
  auto* mask_delete_empty_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskDeleteEmptySelectionsButton"));
  auto* mask_update_generated_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskUpdateGeneratedSelectionsButton"));
  auto* mask_action_retry_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskActionRetryButton"));
  auto* mask_creation_done_button = edit_workspace->findChild<QObject*>(QStringLiteral("maskCreationDoneButton"));
  auto* active_canvas_tool_chip =
      edit_workspace->findChild<QObject*>(QStringLiteral("activeCanvasToolChip"));
  auto* active_canvas_tool_chip_name_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("activeCanvasToolChipNameLabel"));
  auto* active_canvas_tool_chip_mode_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("activeCanvasToolChipModeLabel"));
  auto* active_canvas_tool_done_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("activeCanvasToolDoneButton"));
  auto* active_canvas_tool_cancel_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("activeCanvasToolCancelButton"));
  auto* mask_refine_toolbar = edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineToolbar"));
  auto* mask_refine_toolbar_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineToolbarLabel"));
  auto* mask_refine_edge_add_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineEdgeAddButton"));
  auto* mask_refine_edge_erase_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineEdgeEraseButton"));
  auto* mask_refine_smart_select_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineSmartSelectButton"));
  auto* mask_refine_show_original_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineShowOriginalButton"));
  auto* mask_refine_show_refined_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineShowRefinedButton"));
  auto* mask_refine_high_quality_preview_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineHighQualityPreviewButton"));
  auto* mask_full_resolution_preview_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskFullResolutionPreviewButton"));
  auto* mask_refine_edge_preview_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineEdgePreviewButton"));
  auto* mask_refine_invert_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineInvertButton"));
  auto* mask_refine_duplicate_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineDuplicateButton"));
  auto* mask_refine_duplicate_invert_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineDuplicateInvertButton"));
  auto* mask_refine_duplicate_set_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineDuplicateSetButton"));
  auto* mask_refine_reset_edge_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineResetEdgeButton"));
  auto* active_mask_toolbar_status_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("activeMaskToolbarStatusLabel"));
  auto* mask_stack_strip = find_named_object(edit_workspace, QStringLiteral("maskStackStrip"));
  auto* mask_stack_strip_label = find_named_object(edit_workspace, QStringLiteral("maskStackStripLabel"));
  auto* mask_stack_set_chip = find_named_object(edit_workspace, QStringLiteral("maskStackSetChip"));
  auto* mask_stack_set_label = find_named_object(edit_workspace, QStringLiteral("maskStackSetLabel"));
  auto* mask_stack_set_summary_label =
      find_named_object(edit_workspace, QStringLiteral("maskStackSetSummaryLabel"));
  auto* mask_stack_set_thumbnail =
      find_named_object(edit_workspace, QStringLiteral("maskStackSetThumbnail"));
  auto* mask_stack_set_visibility_button =
      find_named_object(edit_workspace, QStringLiteral("maskStackSetVisibilityButton"));
  auto* mask_stack_set_solo_button =
      find_named_object(edit_workspace, QStringLiteral("maskStackSetSoloButton"));
  auto* mask_advanced_details_button =
      find_named_object(edit_workspace, QStringLiteral("maskAdvancedDetailsButton"));
  auto* mask_stack_source_visibility_button =
      find_named_object(edit_workspace, QStringLiteral("maskStackSourceVisibilityButton"));
  auto* mask_stack_source_solo_button =
      find_named_object(edit_workspace, QStringLiteral("maskStackSourceSoloButton"));
  auto* edit_shared_mask_overlay =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportSharedMaskOverlay"));
  auto* edit_mask_canvas_overlay =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportMaskCanvasOverlay"));
  auto* edit_generic_mask_tint =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportGenericMaskTint"));
  auto* edit_pan_zoom_input_handler =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportPanZoomInputHandler"));
  QVariant mask_creation_context_signature;
  QVariant mask_create_refine_split_signature;
  QVariant mask_refinement_preview_signature;
  QVariant mask_refinement_preview_original;
  QVariant mask_refinement_preview_refined;
  QVariant mask_refinement_high_quality_enabled;
  QVariant mask_full_resolution_preview_enabled;
  QVariant mask_action_retry_signature;
  QVariant mask_work_chrome_signature;
  const auto previous_mask_preview_mode = editor_preview.maskPreviewMode();
  editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
  QCoreApplication::processEvents();
  QVariant edit_outline_animation_idle;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "selectionOutlineAnimationShouldRun",
          Q_RETURN_ARG(QVariant, edit_outline_animation_idle)) ||
      edit_workspace->property("selectionOutlineAnimationContract").toString() !=
          QStringLiteral("visibility-gated|motion-budgeted|point-budgeted:v1") ||
      edit_workspace->property("selectionOutlineAnimationPointBudget").toInt() < 1000 ||
      edit_outline_animation_idle.toBool()) {
    return fail("edit workspace should gate marching-ants animation by visibility, motion, and outline complexity");
  }
  editor_preview.setMaskPreviewMode(previous_mask_preview_mode);
  edit_workspace->setProperty("diagnosticsPanelOpen", true);
  QCoreApplication::processEvents();
  if (mask_creation_toolbar == nullptr || mask_tool_rail == nullptr ||
      mask_tool_rail_toggle_button == nullptr || mask_tool_rail_section_label == nullptr ||
      mask_creation_vocabulary_label == nullptr ||
      mask_source_object_button == nullptr ||
      mask_source_subject_button == nullptr || mask_source_sky_button == nullptr ||
      mask_source_background_button == nullptr ||
      mask_source_brush_add_button == nullptr || mask_source_brush_erase_button == nullptr ||
      mask_source_ellipse_button == nullptr || mask_source_rectangle_button == nullptr ||
      mask_add_button == nullptr || mask_subtract_button == nullptr ||
      mask_intersect_button == nullptr ||
      mask_replace_button == nullptr || mask_delete_empty_button == nullptr ||
      mask_update_generated_button == nullptr ||
      mask_action_retry_button == nullptr ||
      mask_creation_done_button == nullptr || active_canvas_tool_chip == nullptr ||
      active_canvas_tool_chip_name_label == nullptr ||
      active_canvas_tool_chip_mode_label == nullptr ||
      active_canvas_tool_done_button == nullptr ||
      active_canvas_tool_cancel_button == nullptr || mask_refine_toolbar == nullptr ||
      mask_refine_toolbar_label == nullptr || mask_refine_edge_add_button == nullptr ||
      mask_refine_edge_erase_button == nullptr ||
      mask_refine_smart_select_button == nullptr ||
      mask_refine_show_original_button == nullptr ||
      mask_refine_show_refined_button == nullptr ||
      mask_refine_high_quality_preview_button == nullptr ||
      mask_full_resolution_preview_button == nullptr ||
      mask_refine_edge_preview_button == nullptr ||
      mask_refine_invert_button == nullptr ||
      mask_refine_duplicate_button == nullptr || mask_refine_duplicate_invert_button == nullptr ||
      mask_refine_duplicate_set_button == nullptr ||
      mask_refine_reset_edge_button == nullptr ||
      active_mask_toolbar_status_label == nullptr ||
      mask_stack_strip == nullptr || mask_stack_strip_label == nullptr ||
      mask_stack_set_chip == nullptr ||
      mask_stack_set_label == nullptr || mask_stack_set_summary_label == nullptr ||
      mask_stack_set_thumbnail == nullptr ||
      mask_stack_set_visibility_button == nullptr || mask_stack_set_solo_button == nullptr ||
      mask_advanced_details_button == nullptr ||
      mask_stack_source_visibility_button == nullptr || mask_stack_source_solo_button == nullptr ||
      edit_shared_mask_overlay == nullptr ||
      edit_shared_mask_overlay->property("sharedOverlayVersion").toInt() != 1 ||
      edit_mask_canvas_overlay == nullptr || edit_generic_mask_tint == nullptr ||
      edit_pan_zoom_input_handler == nullptr ||
      edit_pan_zoom_input_handler->property("inputHandlerRole").toString() != QStringLiteral("pan-zoom") ||
      edit_pan_zoom_input_handler->property("pointerGrabPolicy").toString() != QStringLiteral("defer-to-mask-overlay") ||
      !mask_creation_toolbar->property("visible").toBool() ||
      mask_creation_vocabulary_label->property("text").toString() != QStringLiteral("Selection") ||
      !mask_creation_vocabulary_label->property("vocabularyTooltip").toString().contains(QStringLiteral("Retouch")) ||
      !mask_creation_vocabulary_label->property("vocabularyTooltip").toString().contains(QStringLiteral("Selection group")) ||
      !mask_creation_vocabulary_label->property("vocabularyTooltip").toString().contains(QStringLiteral("Component")) ||
      !mask_creation_vocabulary_label->property("vocabularyTooltip").toString().contains(QStringLiteral("Operation")) ||
      !mask_creation_vocabulary_label->property("vocabularyTooltip").toString().contains(QStringLiteral("Overlay")) ||
      !mask_tool_rail->property("visible").toBool() ||
      mask_tool_rail->property("layoutContract").toString() !=
          QStringLiteral("left-rail|icon-first|expandable:v1") ||
      !mask_tool_rail->property("compactLabelsHidden").toBool() ||
      mask_tool_rail->property("width").toDouble() > 54.0 ||
      mask_tool_rail_toggle_button->property("text").toString() != QStringLiteral("Tools") ||
      !mask_tool_rail_toggle_button->property("tooltip").toString().contains(QStringLiteral("Expand")) ||
      mask_tool_rail_section_label->property("visible").toBool() ||
      mask_creation_toolbar->property("height").toDouble() > 44.0 ||
      mask_source_object_button->property("text").toString() != QStringLiteral("Object") ||
      mask_source_subject_button->property("text").toString() != QStringLiteral("Subject") ||
      mask_source_sky_button->property("text").toString() != QStringLiteral("Sky") ||
      mask_source_background_button->property("text").toString() !=
          QStringLiteral("Background") ||
      !mask_source_object_button->property("tooltip").toString().contains(editor_preview.smartSelectionRuntimeLabel()) ||
      !mask_source_subject_button->property("tooltip").toString().contains(editor_preview.smartSelectionRuntimeLabel()) ||
      !mask_source_sky_button->property("tooltip").toString().contains(QStringLiteral("not semantic sky segmentation")) ||
      !mask_source_background_button->property("tooltip").toString().contains(QStringLiteral("not a separate background model")) ||
      mask_source_brush_add_button->property("text").toString() != QStringLiteral("Brush +") ||
      mask_source_brush_erase_button->property("text").toString() != QStringLiteral("Erase") ||
      mask_refine_smart_select_button->property("text").toString() != QStringLiteral("Smart") ||
      mask_source_ellipse_button->property("text").toString() != QStringLiteral("Ellipse") ||
      mask_source_rectangle_button->property("text").toString() != QStringLiteral("Rect") ||
      !mask_source_ellipse_button->property("tooltip").toString().contains(QStringLiteral("ellipse")) ||
      !mask_source_rectangle_button->property("tooltip").toString().contains(QStringLiteral("rectangle")) ||
      !mask_source_brush_add_button->property("tooltip").toString().contains(QStringLiteral("additive")) ||
      !mask_source_brush_erase_button->property("tooltip").toString().contains(QStringLiteral("subtractive")) ||
      !mask_add_button->property("visible").toBool() ||
      !mask_replace_button->property("visible").toBool() ||
      mask_delete_empty_button->property("visible").toBool() ||
      mask_update_generated_button->property("visible").toBool() ||
      mask_action_retry_button->property("visible").toBool() ||
      !mask_creation_done_button->property("visible").toBool() ||
      active_canvas_tool_chip->property("layoutContract").toString() !=
          QStringLiteral("active-tool-chip|done-cancel|viewport:v1") ||
      active_canvas_tool_chip->property("visible").toBool() ||
      !mask_refine_toolbar->property("visible").toBool() ||
      mask_refine_toolbar_label->property("text").toString() != QStringLiteral("Refine") ||
      mask_refine_edge_add_button->property("text").toString() != QStringLiteral("Add") ||
      mask_refine_edge_erase_button->property("text").toString() != QStringLiteral("Erase") ||
      !mask_refine_edge_add_button->property("tooltip").toString().contains(QStringLiteral("Brush Refine")) ||
      !mask_refine_edge_erase_button->property("tooltip").toString().contains(QStringLiteral("Brush Refine")) ||
      mask_refine_show_original_button->property("text").toString() != QStringLiteral("Original") ||
      mask_refine_show_refined_button->property("text").toString() != QStringLiteral("Refined") ||
      !mask_refine_show_refined_button->property("active").toBool() ||
      mask_refine_high_quality_preview_button->property("text").toString() != QStringLiteral("HQ") ||
      mask_refine_high_quality_preview_button->property("active").toBool() ||
      mask_refine_edge_preview_button->property("text").toString() != QStringLiteral("Edge") ||
      mask_refine_edge_preview_button->property("active").toBool() ||
      mask_refine_invert_button->property("text").toString() != QStringLiteral("Invert") ||
      mask_refine_duplicate_button->property("text").toString() != QStringLiteral("Copy") ||
      mask_refine_duplicate_invert_button->property("text").toString() !=
          QStringLiteral("Copy Inv") ||
      mask_refine_duplicate_set_button->property("text").toString() != QStringLiteral("Copy All") ||
      !mask_refine_duplicate_button->property("tooltip").toString().contains(QStringLiteral("new id")) ||
      !mask_refine_duplicate_invert_button->property("tooltip").toString().contains(QStringLiteral("background")) ||
      !mask_refine_duplicate_set_button->property("tooltip").toString().contains(QStringLiteral("whole active selection group")) ||
      mask_refine_reset_edge_button->property("text").toString() != QStringLiteral("Reset") ||
      !mask_refine_reset_edge_button->property("tooltip").toString().contains(QStringLiteral("preserving the selection geometry")) ||
      !mask_stack_strip->property("visible").toBool() ||
      mask_stack_strip_label->property("text").toString() != QStringLiteral("Selections") ||
      !mask_stack_set_chip->property("visible").toBool() ||
      !mask_stack_set_label->property("text").toString().contains(QStringLiteral("Linear")) ||
      mask_stack_set_summary_label->property("text").toString() !=
          QStringLiteral("1 component") ||
      !mask_stack_set_chip->property("layoutFits").toBool() ||
      !mask_stack_set_chip->property("layoutSignature").toString().contains(QStringLiteral("fits:true")) ||
      mask_stack_set_chip->property("height").toDouble() > 52.0 ||
      !mask_stack_set_visibility_button->property("visible").toBool() ||
      !mask_stack_set_visibility_button->property("active").toBool() ||
      !mask_stack_set_visibility_button->property("tooltip").toString().contains(QStringLiteral("selection group")) ||
      !mask_stack_set_solo_button->property("visible").toBool() ||
      !mask_stack_set_solo_button->property("tooltip").toString().contains(QStringLiteral("selection group")) ||
      !mask_advanced_details_button->property("visible").toBool() ||
      mask_advanced_details_button->property("active").toBool() ||
      !mask_advanced_details_button->property("tooltip").toString().contains(QStringLiteral("component-level")) ||
      mask_stack_source_visibility_button->property("visible").toBool() ||
      !mask_stack_source_visibility_button->property("active").toBool() ||
      !mask_stack_source_visibility_button->property("tooltip").toString().contains(QStringLiteral("component")) ||
      mask_stack_source_solo_button->property("visible").toBool() ||
      !mask_stack_source_solo_button->property("tooltip").toString().contains(QStringLiteral("component")) ||
      !edit_mask_canvas_overlay->property("visible").toBool() ||
      !editor_preview.activeMaskPreviewSource().isEmpty() ||
      edit_generic_mask_tint->property("visible").toBool() ||
      edit_generic_mask_tint->property("targetOpacity").toDouble() > 0.20 ||
      edit_mask_canvas_overlay->property("previewOverlayOpacity").toDouble() > 0.45 ||
      edit_mask_canvas_overlay->property("selectionOverlayOpacity").toDouble() > 0.68 ||
      edit_mask_canvas_overlay->property("geometricOverlayOpacity").toDouble() > 0.56 ||
      edit_mask_canvas_overlay->property("geometricFillOpacity").toDouble() > 0.12 ||
      edit_mask_canvas_overlay->property("geometricStrokeOpacity").toDouble() > 0.78 ||
      !mask_intersect_button->property("active").toBool() ||
      !active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Linear selection")) ||
      !active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Intersect")) ||
      !active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Normal")) ||
      !active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Outline: Alpha")) ||
      !active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Target:")) ||
      !active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Adjustment layer")) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskCreationContextSignature",
          Q_RETURN_ARG(QVariant, mask_creation_context_signature)) ||
      mask_creation_context_signature.toString() !=
          QStringLiteral("component:Linear selection|operation:Intersect|state:Normal|targetGroup:Linear Mask 1|target:Adjustment layer") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskCreateRefineSplitSignature",
          Q_RETURN_ARG(QVariant, mask_create_refine_split_signature)) ||
      mask_create_refine_split_signature.toString() !=
          QStringLiteral("create:true|refine:true|active:true|role:selection") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskRefinementPreviewSignature",
          Q_RETURN_ARG(QVariant, mask_refinement_preview_signature)) ||
      mask_refinement_preview_signature.toString() !=
          QStringLiteral("refinement-preview:refined|quality:normal|full:false") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setStudioRefinementPreviewMode",
          Q_RETURN_ARG(QVariant, mask_refinement_preview_original),
          Q_ARG(QVariant, QStringLiteral("original"))) ||
      !mask_refinement_preview_original.toBool() ||
      editor_preview.maskRefinementPreviewMode() != QStringLiteral("original") ||
      !mask_refine_show_original_button->property("active").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setStudioRefinementPreviewMode",
          Q_RETURN_ARG(QVariant, mask_refinement_preview_refined),
          Q_ARG(QVariant, QStringLiteral("refined"))) ||
      !mask_refinement_preview_refined.toBool() ||
      editor_preview.maskRefinementPreviewMode() != QStringLiteral("refined") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setStudioRefinementHighQualityPreview",
          Q_RETURN_ARG(QVariant, mask_refinement_high_quality_enabled),
          Q_ARG(QVariant, true)) ||
      !mask_refinement_high_quality_enabled.toBool() ||
      !editor_preview.maskRefinementHighQualityPreview() ||
      editor_preview.activeMaskPreviewQualityTier() != QStringLiteral("high") ||
      !mask_refine_high_quality_preview_button->property("active").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setStudioFullResolutionMaskPreview",
          Q_RETURN_ARG(QVariant, mask_full_resolution_preview_enabled),
          Q_ARG(QVariant, true)) ||
      !mask_full_resolution_preview_enabled.toBool() ||
      !editor_preview.maskFullResolutionPreview() ||
      editor_preview.activeMaskPreviewQualityTier() != QStringLiteral("full") ||
      !mask_full_resolution_preview_button->property("active").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskActionRetrySignature",
          Q_RETURN_ARG(QVariant, mask_action_retry_signature)) ||
      mask_action_retry_signature.toString() !=
          QStringLiteral("available:false|label:|detail:") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskWorkChromeSignature",
          Q_RETURN_ARG(QVariant, mask_work_chrome_signature)) ||
      mask_work_chrome_signature.toString() !=
          QStringLiteral("maskWork:true|chromeReduced:true|diagnosticsPanel:false") ||
      editor_preview.maskCount() != initial_mask_count + 1 ||
      editor_preview.activeMaskOperation() != QStringLiteral("intersect") ||
      editor_preview.maskPreviewMode() != QStringLiteral("overlay")) {
    std::cerr << "Mask creation smoke state:"
              << " stackStrip=" << (mask_stack_strip == nullptr ? "null" : "present")
              << " stackVisible="
              << (mask_stack_strip != nullptr && mask_stack_strip->property("visible").toBool())
              << " chip=" << (mask_stack_set_chip == nullptr ? "null" : "present")
              << " chipVisible="
              << (mask_stack_set_chip != nullptr && mask_stack_set_chip->property("visible").toBool())
              << " chipFits="
              << (mask_stack_set_chip != nullptr && mask_stack_set_chip->property("layoutFits").toBool())
              << " chipLayout="
              << (mask_stack_set_chip == nullptr
                      ? std::string()
                      : mask_stack_set_chip->property("layoutSignature").toString().toStdString())
              << " chipThumb="
              << (mask_stack_set_chip == nullptr
                      ? std::string()
                      : mask_stack_set_chip->property("thumbnailSource").toString().left(24).toStdString())
              << " label="
              << (mask_stack_set_label == nullptr
                      ? std::string()
                      : mask_stack_set_label->property("text").toString().toStdString())
              << " summary="
              << (mask_stack_set_summary_label == nullptr
                      ? std::string()
                      : mask_stack_set_summary_label->property("text").toString().toStdString())
              << " overlay=" << (edit_mask_canvas_overlay == nullptr ? "null" : "present")
              << " overlayVisible="
              << (edit_mask_canvas_overlay != nullptr &&
                  edit_mask_canvas_overlay->property("visible").toBool())
              << " genericTintVisible="
              << (edit_generic_mask_tint != nullptr && edit_generic_mask_tint->property("visible").toBool())
              << " previewEmpty=" << editor_preview.activeMaskPreviewSource().isEmpty()
              << " toolbarText="
              << (active_mask_toolbar_status_label == nullptr
                      ? std::string()
                      : active_mask_toolbar_status_label->property("text").toString().toStdString())
              << " contextSig=" << mask_creation_context_signature.toString().toStdString()
              << " previewOpacity="
              << (edit_mask_canvas_overlay == nullptr
                      ? -1.0
                      : edit_mask_canvas_overlay->property("previewOverlayOpacity").toDouble())
              << " selectionOpacity="
              << (edit_mask_canvas_overlay == nullptr
                      ? -1.0
                      : edit_mask_canvas_overlay->property("selectionOverlayOpacity").toDouble())
              << " geometricOpacity="
              << (edit_mask_canvas_overlay == nullptr
                      ? -1.0
                      : edit_mask_canvas_overlay->property("geometricOverlayOpacity").toDouble())
              << " fillOpacity="
              << (edit_mask_canvas_overlay == nullptr
                      ? -1.0
                      : edit_mask_canvas_overlay->property("geometricFillOpacity").toDouble())
              << " strokeOpacity="
              << (edit_mask_canvas_overlay == nullptr
                      ? -1.0
                      : edit_mask_canvas_overlay->property("geometricStrokeOpacity").toDouble())
              << " maskCount=" << editor_preview.maskCount()
              << " activeOp=" << editor_preview.activeMaskOperation().toStdString()
              << " previewMode=" << editor_preview.maskPreviewMode().toStdString() << "\n";
    return fail("mask creation should apply the selected combine mode and immediately show the mask in the stack and on canvas");
  }
  const auto mask_count_before_object_pick = editor_preview.maskCount();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "createMaskSource",
          Q_ARG(QVariant, QVariant(QStringLiteral("object"))))) {
    return fail("Studio Object source should be invokable from the selection rail");
  }
  QCoreApplication::processEvents();
  if (!edit_workspace->property("objectPickArmed").toBool() ||
      !edit_workspace->property("objectPickCreateNew").toBool() ||
      edit_workspace->property("objectPickMode").toString() != QStringLiteral("selection") ||
      editor_preview.maskCount() != mask_count_before_object_pick ||
      !active_canvas_tool_chip->property("visible").toBool() ||
      active_canvas_tool_chip_name_label->property("text").toString() != QStringLiteral("Object") ||
      !active_canvas_tool_chip_mode_label->property("text").toString().contains(
          QStringLiteral("Hover or drag")) ||
      active_canvas_tool_done_button->property("enabled").toBool() ||
      !active_canvas_tool_cancel_button->property("enabled").toBool()) {
    return fail("Studio Object selection should arm a cancelable pre-commit tool without creating a placeholder mask");
  }
  QVariant object_pick_cancelled;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "cancelActiveTool",
          Q_RETURN_ARG(QVariant, object_pick_cancelled)) ||
      !object_pick_cancelled.toBool() ||
      edit_workspace->property("objectPickArmed").toBool() ||
      edit_workspace->property("objectPickCreateNew").toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("masks") ||
      active_canvas_tool_chip->property("visible").toBool() ||
      editor_preview.maskCount() != mask_count_before_object_pick) {
    return fail("Studio Object selection Cancel/Escape should exit the armed tool without changing masks");
  }
  const QVector<QObject*> compact_mask_label_buttons = {
      mask_source_object_button,
      mask_source_subject_button,
      mask_source_sky_button,
      mask_source_background_button,
      mask_source_brush_add_button,
      mask_source_brush_erase_button,
      mask_source_ellipse_button,
      mask_source_rectangle_button,
      mask_refine_smart_select_button,
      mask_refine_show_original_button,
      mask_refine_show_refined_button,
      mask_refine_high_quality_preview_button,
      mask_refine_edge_preview_button,
      mask_refine_duplicate_button,
      mask_refine_duplicate_invert_button,
      mask_refine_duplicate_set_button,
      mask_refine_reset_edge_button,
  };
  for (const auto* button : compact_mask_label_buttons) {
    const auto text = button->property("text").toString();
    const bool exposes_set_word =
        text.compare(QStringLiteral("Set"), Qt::CaseInsensitive) == 0 ||
        text.contains(QStringLiteral(" Set"), Qt::CaseInsensitive) ||
        text.contains(QStringLiteral("Set "), Qt::CaseInsensitive);
    if (text.size() > 10 ||
        text.contains(QStringLiteral("Source"), Qt::CaseInsensitive) ||
        exposes_set_word ||
        text.contains(QStringLiteral("Stack"), Qt::CaseInsensitive)) {
      return fail("primary masking labels should stay compact and avoid technical source/set/stack wording");
    }
  }
  const auto mask_refine_toolbar_text = mask_refine_toolbar_label->property("text").toString();
  const bool toolbar_exposes_set_word =
      mask_refine_toolbar_text.compare(QStringLiteral("Set"), Qt::CaseInsensitive) == 0 ||
      mask_refine_toolbar_text.contains(QStringLiteral(" Set"), Qt::CaseInsensitive) ||
      mask_refine_toolbar_text.contains(QStringLiteral("Set "), Qt::CaseInsensitive);
  if (mask_refine_toolbar_text.size() > 10 ||
      mask_refine_toolbar_text.contains(QStringLiteral("Source"), Qt::CaseInsensitive) ||
      toolbar_exposes_set_word ||
      mask_refine_toolbar_text.contains(QStringLiteral("Stack"), Qt::CaseInsensitive)) {
    return fail("primary masking labels should stay compact and avoid technical source/set/stack wording");
  }
  QVariant edit_overlay_geometry_signature;
  if (!QMetaObject::invokeMethod(
          edit_shared_mask_overlay,
          "overlayGeometrySignature",
          Q_RETURN_ARG(QVariant, edit_overlay_geometry_signature)) ||
      edit_shared_mask_overlay->property("overlayGeometryContract").toString() !=
          QStringLiteral("image-rect-clipped|crop-rotation-aware:v1") ||
      !edit_overlay_geometry_signature.toString().startsWith(
          QStringLiteral("image-rect-clipped|crop-rotation-aware:v1|clip:true|")) ||
      !edit_overlay_geometry_signature.toString().contains(QStringLiteral("|crop:")) ||
      !edit_overlay_geometry_signature.toString().contains(QStringLiteral("|rotation:"))) {
    return fail("edit shared mask overlay should be clipped to the image rect and expose crop/rotation-aware geometry");
  }
  editor_preview.setMaskRefinementHighQualityPreview(false);
  editor_preview.setMaskPreviewMode(QStringLiteral("refinement-edge"));
  QCoreApplication::processEvents();
  if (editor_preview.maskPreviewMode() != QStringLiteral("overlay") ||
      mask_refine_edge_preview_button->property("active").toBool() ||
      mask_refine_edge_preview_button->property("visible").toBool() ||
      mask_refine_edge_preview_button->property("enabled").toBool()) {
    return fail("Refine Edge diagnostic preview should be hidden and stale refinement-edge requests should fall back to clean overlay mode");
  }
  editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
  edit_workspace->setProperty("diagnosticsPanelOpen", false);
  QCoreApplication::processEvents();
  const auto active_mask_set_name = editor_preview.activeMaskSetName();
  if (active_mask_set_name.isEmpty() ||
      !active_mask_toolbar_status_label->property("text").toString().contains(active_mask_set_name) ||
      !mask_add_button->property("tooltip").toString().contains(active_mask_set_name) ||
      !mask_subtract_button->property("tooltip").toString().contains(active_mask_set_name) ||
      !mask_intersect_button->property("tooltip").toString().contains(active_mask_set_name) ||
      !mask_replace_button->property("tooltip").toString().contains(active_mask_set_name)) {
    return fail("mask combine controls should name the target mask set for every operation");
  }
  const auto mask_count_before_layout_proof = editor_preview.maskCount();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskCreationOperation",
          Q_ARG(QVariant, QVariant(QStringLiteral("add")))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "createMaskSource",
          Q_ARG(QVariant, QVariant(QStringLiteral("radial"))))) {
    return fail("mask row layout proof should be able to add a second source to the active mask set");
  }
  editor_preview.deselectActiveMask();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "createMaskSource",
          Q_ARG(QVariant, QVariant(QStringLiteral("rectangle"))))) {
    return fail("mask row layout proof should be able to add a second mask set");
  }
  editor_preview.startRemoveBrush();
  editor_preview.startHealBrush();
  QQmlComponent mask_view_layout_component(
      &engine,
      QUrl::fromLocalFile(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                              .filePath(QStringLiteral("apps/desktop/qml/workspaces/MaskView.qml"))));
  if (mask_view_layout_component.isError()) {
    for (const auto& error : mask_view_layout_component.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
    return fail("mask row layout proof should load MaskView.qml");
  }
  QObject* mask_view_layout_object = mask_view_layout_component.create(engine.rootContext());
  auto* mask_view_layout_item = qobject_cast<QQuickItem*>(mask_view_layout_object);
  if (mask_view_layout_item == nullptr) {
    delete mask_view_layout_object;
    return fail("mask row layout proof should instantiate MaskView as a QQuickItem");
  }
  mask_view_layout_item->setParentItem(root_window->contentItem());
  mask_view_layout_item->setVisible(true);
  mask_view_layout_item->setWidth(784.0);
  mask_view_layout_item->setHeight(900.0);
  process_layout_events(12);
  auto* mask_layout_split = find_named_item(mask_view_layout_item, QStringLiteral("maskWorkspaceStableSplit"));
  auto* mask_layout_canvas = find_named_item(mask_view_layout_item, QStringLiteral("maskCanvasPane"));
  auto* mask_layout_inspector = find_named_item(mask_view_layout_item, QStringLiteral("maskInspectorPane"));
  auto* mask_layout_scroll = find_named_item(mask_view_layout_item, QStringLiteral("maskInspectorScrollView"));
  auto* mask_view_pointer_layer =
      find_named_item(mask_view_layout_item, QStringLiteral("maskViewPointerHandlerLayer"));
  auto* mask_view_hover_handler =
      find_named_object(mask_view_layout_item, QStringLiteral("maskViewToolHoverHandler"));
  auto* mask_view_smart_point_handler =
      find_named_object(mask_view_layout_item, QStringLiteral("maskViewSmartSelectPointHandler"));
  auto* mask_view_lasso_point_handler =
      find_named_object(mask_view_layout_item, QStringLiteral("maskViewLassoPointHandler"));
  auto* mask_view_polygon_tap_handler =
      find_named_object(mask_view_layout_item, QStringLiteral("maskViewPolygonTapHandler"));
  auto* mask_view_shared_overlay =
      find_named_item(mask_view_layout_item, QStringLiteral("maskViewSharedMaskOverlay"));
  auto* mask_view_canvas_overlay =
      find_named_item(mask_view_layout_item, QStringLiteral("maskViewMaskCanvasOverlay"));
  auto* mask_view_geometric_canvas =
      find_named_item(mask_view_layout_item, QStringLiteral("maskViewGeometricMaskCanvas"));
  editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
  QCoreApplication::processEvents();
  QVariant mask_view_outline_animation_idle;
  QVariant mask_view_overlay_geometry_signature;
  if (!QMetaObject::invokeMethod(
          mask_view_layout_item,
          "selectionOutlineAnimationShouldRun",
          Q_RETURN_ARG(QVariant, mask_view_outline_animation_idle))) {
    return fail("mask workspace should expose a testable marching-ants animation gate");
  }
  if (mask_view_shared_overlay != nullptr &&
      !QMetaObject::invokeMethod(
          mask_view_shared_overlay,
          "overlayGeometrySignature",
          Q_RETURN_ARG(QVariant, mask_view_overlay_geometry_signature))) {
    return fail("mask workspace shared overlay should expose a testable geometry signature");
  }
  const auto mask_layout_width =
      mask_view_layout_item->property("minimumCanvasWidth").toInt() +
      mask_view_layout_item->property("stableInspectorWidth").toInt() + 14;
  QString mask_row_overlap_detail;
  const bool mask_rows_layout_ok =
      mask_view_layout_item->width() == mask_layout_width &&
      item_visible(mask_layout_split) && item_visible(mask_layout_canvas) &&
      item_visible(mask_layout_inspector) && item_visible(mask_layout_scroll) &&
      mask_view_pointer_layer != nullptr &&
      mask_view_hover_handler != nullptr &&
      mask_view_smart_point_handler != nullptr &&
      mask_view_smart_point_handler->property("pointerGrabPolicy").toString() ==
          QStringLiteral("exclusive-smart-selection-stroke") &&
      mask_view_lasso_point_handler != nullptr &&
      mask_view_lasso_point_handler->property("pointerGrabPolicy").toString() ==
          QStringLiteral("exclusive-lasso-drag") &&
      mask_view_polygon_tap_handler != nullptr &&
      mask_view_polygon_tap_handler->property("pointerGrabPolicy").toString() ==
          QStringLiteral("exclusive-polygon-tap") &&
      mask_view_shared_overlay != nullptr &&
      mask_view_shared_overlay->property("sharedOverlayVersion").toInt() == 1 &&
      mask_view_shared_overlay->property("overlayGeometryContract").toString() ==
          QStringLiteral("image-rect-clipped|crop-rotation-aware:v1") &&
      mask_view_overlay_geometry_signature.toString().startsWith(
          QStringLiteral("image-rect-clipped|crop-rotation-aware:v1|clip:true|")) &&
      mask_view_canvas_overlay != nullptr &&
      mask_view_canvas_overlay->property("outlineRenderContract").toString() ==
          QStringLiteral("anti-aliased|zoom-stable|pixel-aligned:v1") &&
      mask_view_geometric_canvas != nullptr &&
      mask_view_geometric_canvas->property("outlineRenderContract").toString() ==
          QStringLiteral("anti-aliased|zoom-stable|pixel-aligned:v1") &&
      mask_view_layout_item->property("selectionOutlineAnimationContract").toString() ==
          QStringLiteral("visibility-gated|motion-budgeted|point-budgeted:v1") &&
      mask_view_layout_item->property("selectionOutlineAnimationPointBudget").toInt() >= 1000 &&
      !mask_view_outline_animation_idle.toBool() &&
      mask_layout_canvas->width() >= mask_view_layout_item->property("minimumCanvasWidth").toDouble() &&
      std::abs(mask_layout_inspector->width() -
               mask_view_layout_item->property("stableInspectorWidth").toDouble()) <= 1.0 &&
      visible_named_item_instances_do_not_overlap(
          mask_view_layout_item, QStringLiteral("maskSetFocusRow"), 2, &mask_row_overlap_detail) &&
      visible_named_item_instances_do_not_overlap(
          mask_view_layout_item, QStringLiteral("maskSourceFocusRow"), 3, &mask_row_overlap_detail) &&
      visible_named_item_instances_do_not_overlap(
          mask_view_layout_item, QStringLiteral("retouchFocusRow"), 2, &mask_row_overlap_detail);
  const auto actual_mask_layout_width = mask_view_layout_item->width();
  mask_view_layout_item->setVisible(false);
  mask_view_layout_item->setParentItem(nullptr);
  delete mask_view_layout_object;
  int mask_layout_cleanup_guard = 0;
  while (editor_preview.maskCount() > mask_count_before_layout_proof &&
         mask_layout_cleanup_guard < 64) {
    ++mask_layout_cleanup_guard;
    editor_preview.setActiveMaskIndex(editor_preview.maskCount() - 1);
    editor_preview.removeActiveMask();
  }
  if (editor_preview.maskCount() > mask_count_before_layout_proof) {
    return fail("mask row layout proof cleanup should remove temporary masks without hanging");
  }
  if (mask_count_before_layout_proof > 0) {
    editor_preview.setActiveMaskIndex(mask_count_before_layout_proof - 1);
  }
  editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
  edit_workspace->setProperty("editTool", QStringLiteral("masks"));
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  process_layout_events(6);
  if (!mask_rows_layout_ok || editor_preview.maskCount() != mask_count_before_layout_proof) {
    std::cerr << "Mask row layout proof detail: "
              << mask_row_overlap_detail.toStdString()
              << " width=" << actual_mask_layout_width
              << " expectedWidth=" << mask_layout_width
              << " pointerLayer=" << (mask_view_pointer_layer == nullptr ? "null" : "present")
              << " hoverHandler=" << (mask_view_hover_handler == nullptr ? "null" : "present")
              << " smartHandler=" << (mask_view_smart_point_handler == nullptr ? "null" : "present")
              << " lassoHandler=" << (mask_view_lasso_point_handler == nullptr ? "null" : "present")
              << " polygonTapHandler=" << (mask_view_polygon_tap_handler == nullptr ? "null" : "present")
              << " sharedOverlay=" << (mask_view_shared_overlay == nullptr ? "null" : "present")
              << " canvasOverlay=" << (mask_view_canvas_overlay == nullptr ? "null" : "present")
              << " geometricCanvas=" << (mask_view_geometric_canvas == nullptr ? "null" : "present")
              << " restoredMaskCount=" << editor_preview.maskCount()
              << " expectedMaskCount=" << mask_count_before_layout_proof << '\n';
    return fail("MaskView rows should not overlap at the minimum supported canvas plus inspector width");
  }
  editor_preview.setActiveMaskLocked(true);
  QCoreApplication::processEvents();
  if (editor_preview.activeMaskEditDisabledReason().isEmpty() ||
      mask_refine_duplicate_button->property("enabled").toBool() ||
      mask_refine_duplicate_invert_button->property("enabled").toBool() ||
      mask_refine_invert_button->property("enabled").toBool() ||
      !mask_refine_duplicate_button->property("tooltip").toString().contains(QStringLiteral("Unlock it"))) {
    return fail("locked selection sources should disable active-source edits with a clear reason");
  }
  editor_preview.setActiveMaskLocked(false);
  QCoreApplication::processEvents();
  if (!editor_preview.activeMaskEditDisabledReason().isEmpty() ||
      !mask_refine_duplicate_button->property("enabled").toBool() ||
      !mask_refine_duplicate_invert_button->property("enabled").toBool() ||
      !mask_refine_invert_button->property("enabled").toBool()) {
    return fail("unlocking a selection source should restore active-source edit actions");
  }
  editor_preview.addMask(QStringLiteral("polygon"));
  QCoreApplication::processEvents();
  if (editor_preview.emptyMaskCount() != 1 ||
      !mask_delete_empty_button->property("visible").toBool() ||
      !mask_delete_empty_button->property("enabled").toBool() ||
      !mask_delete_empty_button->property("tooltip").toString().contains(QStringLiteral("edit recipe")) ||
      !active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Empty selections: 1")) ||
      editor_preview.deleteEmptyMasks() != 1) {
    return fail("empty selection sources should be visibly invalid and offer Delete Empty Masks");
  }
  QCoreApplication::processEvents();
  if (editor_preview.emptyMaskCount() != 0 ||
      mask_delete_empty_button->property("visible").toBool() ||
      active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Empty selections")) ||
      editor_preview.maskCount() != initial_mask_count + 1) {
    return fail("Delete Empty Masks should remove invalid sources and clear the toolbar warning");
  }
  auto* polygon_cancel_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("polygonCancelShortcut"));
  auto* polygon_complete_return_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("polygonCompleteReturnShortcut"));
  auto* polygon_complete_enter_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("polygonCompleteEnterShortcut"));
  auto* polygon_backspace_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("polygonRemoveLastPointBackspaceShortcut"));
  auto* polygon_delete_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("polygonRemoveLastPointDeleteShortcut"));
  auto* lasso_complete_return_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("lassoCompleteReturnShortcut"));
  auto* lasso_complete_enter_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("lassoCompleteEnterShortcut"));
  const auto mask_count_before_polygon_cancel = editor_preview.maskCount();
  editor_preview.startPolygonSelection(false);
  editor_preview.appendActivePolygonPoint(0.16, 0.18);
  editor_preview.appendActivePolygonPoint(0.68, 0.20);
  QCoreApplication::processEvents();
  QObject* polygon_hud = nullptr;
  QObject* polygon_hud_complete_button = nullptr;
  QObject* polygon_hud_cancel_button = nullptr;
  QObject* polygon_hud_simplify_button = nullptr;
  QObject* polygon_hud_smooth_button = nullptr;
  if (!wait_for_condition([&]() {
        polygon_hud = find_named_object(edit_workspace, QStringLiteral("polygonToolHud"));
        polygon_hud_complete_button =
            find_named_object(edit_workspace, QStringLiteral("polygonHudCompleteButton"));
        polygon_hud_cancel_button =
            find_named_object(edit_workspace, QStringLiteral("polygonHudCancelButton"));
        polygon_hud_simplify_button =
            find_named_object(edit_workspace, QStringLiteral("polygonHudSimplifyButton"));
        polygon_hud_smooth_button =
            find_named_object(edit_workspace, QStringLiteral("polygonHudSmoothButton"));
        return polygon_hud != nullptr && polygon_hud->property("visible").toBool() &&
               polygon_hud_complete_button != nullptr && polygon_hud_cancel_button != nullptr &&
               polygon_hud_simplify_button != nullptr && polygon_hud_smooth_button != nullptr;
      }) ||
      !active_canvas_tool_chip->property("visible").toBool() ||
      active_canvas_tool_chip_name_label->property("text").toString() != QStringLiteral("Polygon") ||
      !active_canvas_tool_chip_mode_label->property("text").toString().contains(QStringLiteral("2 pts")) ||
      active_canvas_tool_done_button->property("enabled").toBool() ||
      !active_canvas_tool_cancel_button->property("enabled").toBool() ||
      polygon_hud_complete_button->property("enabled").toBool() ||
      !polygon_hud_cancel_button->property("enabled").toBool()) {
    return fail("polygon drawing mode should show cancel and a disabled active-tool Done until enough points exist");
  }
  QVariant polygon_cancelled;
  if (polygon_cancel_shortcut == nullptr || polygon_complete_return_shortcut == nullptr ||
      polygon_complete_enter_shortcut == nullptr || polygon_backspace_shortcut == nullptr ||
      polygon_delete_shortcut == nullptr || !polygon_cancel_shortcut->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "cancelActivePolygonDrawing",
          Q_RETURN_ARG(QVariant, polygon_cancelled)) ||
      !polygon_cancelled.toBool() ||
      editor_preview.maskCount() != mask_count_before_polygon_cancel) {
    return fail("Escape should cancel only the unfinished active polygon selection");
  }
  editor_preview.startPolygonSelection(false);
  editor_preview.appendActivePolygonPoint(0.16, 0.18);
  editor_preview.appendActivePolygonPoint(0.68, 0.20);
  editor_preview.appendActivePolygonPoint(0.62, 0.74);
  QCoreApplication::processEvents();
  QVariant polygon_point_removed;
  if (!polygon_complete_return_shortcut->property("enabled").toBool() ||
      !polygon_complete_enter_shortcut->property("enabled").toBool() ||
      !polygon_hud_complete_button->property("enabled").toBool() ||
      polygon_hud_cancel_button->property("enabled").toBool() ||
      polygon_hud_simplify_button->property("enabled").toBool() ||
      !polygon_hud_smooth_button->property("enabled").toBool() ||
      !polygon_backspace_shortcut->property("enabled").toBool() ||
      !polygon_delete_shortcut->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "removeLastActivePolygonPointFromShortcut",
          Q_RETURN_ARG(QVariant, polygon_point_removed)) ||
      !polygon_point_removed.toBool() ||
      editor_preview.activeMaskPolygonPointCount() != 2) {
    return fail("Backspace/Delete should remove the last active polygon point without clearing the mask");
  }
  editor_preview.appendActivePolygonPoint(0.62, 0.74);
  QVariant polygon_completed;
  if (!active_canvas_tool_done_button->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "finishActiveCanvasTool",
          Q_RETURN_ARG(QVariant, polygon_completed)) ||
      !polygon_completed.toBool() || editor_preview.activeMaskKind() != QStringLiteral("polygon") ||
      editor_preview.activeMaskPolygonPointCount() != 3) {
    return fail("Enter/Return or Done should complete a valid polygon selection without altering its points");
  }
  editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
  process_layout_events(8);
  QQuickItem* shared_outline_hit_target = nullptr;
  QObject* shared_mask_overlay = nullptr;
  if (!wait_for_condition([&]() {
        shared_mask_overlay =
            find_named_object(edit_workspace, QStringLiteral("editImageViewportSharedMaskOverlay"));
        shared_outline_hit_target =
            find_named_item(edit_workspace, QStringLiteral("editImageViewportSharedActiveOutlineHitTarget"));
        return shared_mask_overlay != nullptr &&
               shared_outline_hit_target != nullptr &&
               item_visible(shared_outline_hit_target) &&
               editor_preview.activeMaskOutlinePaths().size() > 0;
      })) {
    std::cerr << "Canvas outline sync state: sharedOverlay="
              << (shared_mask_overlay == nullptr ? "null" : "present")
              << " hitTarget=" << (shared_outline_hit_target == nullptr ? "null" : "present")
              << " hitVisible=" << item_visible(shared_outline_hit_target)
              << " outlinePathCount=" << editor_preview.activeMaskOutlinePaths().size()
              << " hasActiveMask=" << editor_preview.hasActiveMask()
              << " previewMode=" << editor_preview.maskPreviewMode().toStdString() << '\n';
    return fail("completed polygon selections should expose a clickable shared canvas outline");
  }
  QVariant canvas_outline_base_target;
  QVariant canvas_outline_selected;
  QVariant canvas_outline_scope;
  const auto runtime_outline_paths = editor_preview.activeMaskOutlinePaths();
  QVariantList runtime_outline_path;
  if (!runtime_outline_paths.isEmpty()) {
    if (runtime_outline_paths.first().toMap().contains(QStringLiteral("x"))) {
      runtime_outline_path = runtime_outline_paths;
    } else {
      runtime_outline_path = runtime_outline_paths.first().toList();
    }
  }
  if (runtime_outline_path.size() < 2) {
    return fail("canvas outline sync proof should have at least one traced outline segment");
  }
  const auto outline_hit_start = runtime_outline_path.at(0).toMap();
  const auto outline_hit_end = runtime_outline_path.at(1).toMap();
  const auto outline_hit_normalized_x =
      (outline_hit_start.value(QStringLiteral("x")).toDouble() +
       outline_hit_end.value(QStringLiteral("x")).toDouble()) /
      2.0;
  const auto outline_hit_normalized_y =
      (outline_hit_start.value(QStringLiteral("y")).toDouble() +
       outline_hit_end.value(QStringLiteral("y")).toDouble()) /
      2.0;
  const auto outline_hit_x = shared_outline_hit_target->width() * outline_hit_normalized_x;
  const auto outline_hit_y = shared_outline_hit_target->height() * outline_hit_normalized_y;
  QVariant canvas_outline_direct_hit;
  const QVariantList runtime_outline_paths_for_qml =
      runtime_outline_paths.first().toMap().contains(QStringLiteral("x"))
          ? QVariantList{QVariant(runtime_outline_path)}
          : runtime_outline_paths;
  if (!QMetaObject::invokeMethod(
          shared_mask_overlay,
          "outlinePathsHitTest",
          Q_RETURN_ARG(QVariant, canvas_outline_direct_hit),
          Q_ARG(QVariant, outline_hit_x),
          Q_ARG(QVariant, outline_hit_y),
          Q_ARG(QVariant, runtime_outline_paths_for_qml),
          Q_ARG(QVariant, 12.0)) ||
      !canvas_outline_direct_hit.toBool()) {
    std::cerr << "Canvas outline direct hit-test state: directHit="
              << canvas_outline_direct_hit.toBool()
              << " normalizedHit=" << outline_hit_normalized_x << ","
              << outline_hit_normalized_y
              << " pixelHit=" << outline_hit_x << "," << outline_hit_y
              << " pathCount=" << runtime_outline_paths.size()
              << " segmentPointCount=" << runtime_outline_path.size() << '\n';
    return fail("shared canvas outline hit-test should accept a point on the traced outline segment");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setSelectedEditTarget",
          Q_RETURN_ARG(QVariant, canvas_outline_base_target),
          Q_ARG(QVariant, QStringLiteral("base-image")),
          Q_ARG(QVariant, -1),
          Q_ARG(QVariant, QVariantMap{}),
          Q_ARG(QVariant, -1)) ||
      !canvas_outline_base_target.toBool()) {
    return fail("canvas outline sync proof should switch away from the mask target before clicking the outline");
  }
  process_layout_events(4);
  if (!QMetaObject::invokeMethod(
          shared_mask_overlay,
          "selectActiveOutlineFromCanvas",
          Q_RETURN_ARG(QVariant, canvas_outline_selected),
          Q_ARG(QVariant, outline_hit_x),
          Q_ARG(QVariant, outline_hit_y)) ||
      !canvas_outline_selected.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "activeEditTargetScope",
          Q_RETURN_ARG(QVariant, canvas_outline_scope)) ||
      canvas_outline_scope.toString() != QStringLiteral("mask") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("masks") ||
      !editor_preview.hasActiveMask()) {
    std::cerr << "Canvas outline selection state: selected="
              << canvas_outline_selected.toBool()
              << " scope=" << canvas_outline_scope.toString().toStdString()
              << " tool=" << edit_workspace->property("editTool").toString().toStdString()
              << " hit=" << outline_hit_x << "," << outline_hit_y
              << " hitTarget=" << shared_outline_hit_target->width() << "x"
              << shared_outline_hit_target->height() << '\n';
    return fail("clicking the shared canvas outline should reselect its mask/layer target");
  }
  editor_preview.removeActiveMask();
  QCoreApplication::processEvents();
  if (editor_preview.maskCount() != mask_count_before_polygon_cancel) {
    return fail("polygon shortcut smoke cleanup should preserve unrelated masks");
  }
  editor_preview.startPolygonSelection(false);
  editor_preview.appendActivePolygonPoint(0.18, 0.18);
  editor_preview.appendActivePolygonPoint(0.72, 0.22);
  editor_preview.appendActivePolygonPoint(0.58, 0.78);
  QCoreApplication::processEvents();
  QObject* polygon_snap_close_target = nullptr;
  if (!wait_for_condition([&]() {
        polygon_snap_close_target =
            find_named_object(edit_workspace, QStringLiteral("polygonSnapCloseTarget"));
        return polygon_snap_close_target != nullptr &&
               polygon_snap_close_target->property("visible").toBool();
      })) {
    return fail("polygon drawing mode should show a larger snap-to-first close target");
  }
  QVariant far_polygon_snap;
  QVariant near_polygon_completed;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activePolygonSnapClose",
          Q_RETURN_ARG(QVariant, far_polygon_snap),
          Q_ARG(QVariant, QVariant(0.40)),
          Q_ARG(QVariant, QVariant(0.40)),
          Q_ARG(QVariant, QVariant(1000)),
          Q_ARG(QVariant, QVariant(1000))) ||
      far_polygon_snap.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "completeActivePolygonIfSnappingToFirst",
          Q_RETURN_ARG(QVariant, near_polygon_completed),
          Q_ARG(QVariant, QVariant(0.192)),
          Q_ARG(QVariant, QVariant(0.18)),
          Q_ARG(QVariant, QVariant(1000)),
          Q_ARG(QVariant, QVariant(1000))) ||
      !near_polygon_completed.toBool() ||
      editor_preview.activeMaskPolygonPointCount() != 3) {
    return fail("clicking inside the first-point snap target should complete without adding a point");
  }
  QObject* polygon_feather_canvas = nullptr;
  if (!wait_for_condition([&]() {
        polygon_feather_canvas =
            find_named_object(edit_workspace, QStringLiteral("editImageViewportGeometricMaskCanvas"));
        if (polygon_feather_canvas == nullptr) {
          polygon_feather_canvas = find_named_object(edit_workspace, QStringLiteral("geometricMaskCanvas"));
        }
        return polygon_feather_canvas != nullptr &&
               polygon_feather_canvas->property("polygonFeatherVisualizationActive").toBool() &&
               polygon_feather_canvas->property("polygonFeatherVisualizationWidth").toDouble() > 0.0;
      })) {
    return fail("completed feathered polygon selections should expose a visible feather boundary visualization");
  }
  QObject* polygon_vertex_handle = nullptr;
  if (!wait_for_condition([&]() {
        polygon_vertex_handle = find_named_object(edit_workspace, QStringLiteral("polygonVertexHandle1"));
        return polygon_vertex_handle != nullptr && polygon_vertex_handle->property("visible").toBool();
      })) {
    return fail("completed polygon selections should expose draggable vertex handles in Studio");
  }
  if (polygon_vertex_handle->property("handleReadabilityContract").toString() !=
      QStringLiteral("dual-contrast-halo|large-hit-target|color-independent:v1") ||
      polygon_vertex_handle->property("width").toDouble() < 14.0) {
    return fail("Studio polygon vertex handles should expose a dual-contrast readable handle contract");
  }
  if (!editor_preview.moveActivePolygonPoint(1, 0.40, 0.45)) {
    return fail("Studio polygon vertex handles should be backed by editable controller geometry");
  }
  QCoreApplication::processEvents();
  const auto edited_polygon_points = editor_preview.activeMaskPolygonPoints();
  const auto edited_polygon_point = edited_polygon_points.at(1).toMap();
  if (std::abs(edited_polygon_point.value(QStringLiteral("x")).toDouble() - 0.40) > 1e-6 ||
      std::abs(edited_polygon_point.value(QStringLiteral("y")).toDouble() - 0.45) > 1e-6) {
    return fail("moving a polygon vertex should update the active polygon point coordinates");
  }
  QObject* polygon_edge_insert_handle = nullptr;
  if (!wait_for_condition([&]() {
        polygon_edge_insert_handle =
            find_named_object(edit_workspace, QStringLiteral("polygonEdgeInsertHandle1"));
        return polygon_edge_insert_handle != nullptr &&
               polygon_edge_insert_handle->property("visible").toBool();
      })) {
    return fail("completed polygon selections should expose insert handles on polygon edges in Studio");
  }
  if (polygon_edge_insert_handle->property("handleReadabilityContract").toString() !=
      QStringLiteral("dual-contrast-halo|large-hit-target|color-independent:v1") ||
      polygon_edge_insert_handle->property("width").toDouble() < 14.0) {
    return fail("Studio polygon edge insert handles should expose a dual-contrast readable handle contract");
  }
  if (!editor_preview.insertActivePolygonPointAfter(1, 0.50, 0.52) ||
      editor_preview.activeMaskPolygonPointCount() != 4) {
    return fail("Studio polygon edge insert handles should be backed by editable controller geometry");
  }
  QCoreApplication::processEvents();
  const auto inserted_polygon_points = editor_preview.activeMaskPolygonPoints();
  const auto inserted_polygon_point = inserted_polygon_points.at(2).toMap();
  if (std::abs(inserted_polygon_point.value(QStringLiteral("x")).toDouble() - 0.50) > 1e-6 ||
      std::abs(inserted_polygon_point.value(QStringLiteral("y")).toDouble() - 0.52) > 1e-6) {
    return fail("inserting a polygon edge point should add the point after the selected edge start");
  }
  editor_preview.removeActiveMask();
  QCoreApplication::processEvents();
  if (editor_preview.maskCount() != mask_count_before_polygon_cancel) {
    return fail("polygon snap-close smoke cleanup should preserve unrelated masks");
  }
  editor_preview.startLassoSelection(false);
  editor_preview.appendActivePolygonPoint(0.18, 0.18);
  editor_preview.appendActivePolygonPoint(0.36, 0.20);
  editor_preview.appendActivePolygonPoint(0.64, 0.26);
  editor_preview.appendActivePolygonPoint(0.70, 0.68);
  editor_preview.appendActivePolygonPoint(0.28, 0.72);
  QCoreApplication::processEvents();
  QObject* lasso_geometric_canvas = nullptr;
  if (!wait_for_condition([&]() {
        lasso_geometric_canvas =
            find_named_object(edit_workspace, QStringLiteral("editImageViewportGeometricMaskCanvas"));
        return lasso_geometric_canvas != nullptr &&
               lasso_geometric_canvas->property("visible").toBool() &&
               lasso_geometric_canvas->property("liveLassoFillVisible").toBool() &&
               lasso_geometric_canvas->property("liveLassoOutlineVisible").toBool();
      }) ||
      lasso_geometric_canvas->property("liveLassoRenderContract").toString() !=
          QStringLiteral("live-lasso|solid-fill|high-contrast-outline:v1")) {
    return fail("active lasso drawing should show a solid fill and high-contrast outline before release");
  }
  if (lasso_complete_return_shortcut == nullptr || lasso_complete_enter_shortcut == nullptr ||
      !lasso_complete_return_shortcut->property("enabled").toBool() ||
      !lasso_complete_enter_shortcut->property("enabled").toBool() ||
      !active_canvas_tool_chip->property("visible").toBool() ||
      active_canvas_tool_chip_name_label->property("text").toString() != QStringLiteral("Lasso") ||
      !active_canvas_tool_chip_mode_label->property("text").toString().contains(QStringLiteral("5 pts")) ||
      !active_canvas_tool_done_button->property("enabled").toBool()) {
    return fail("active lasso drawing should expose a Done-ready active-tool chip and Enter/Return shortcuts");
  }
  if (!QMetaObject::invokeMethod(
          edit_viewport,
          "handleMaskPointerReleased")) {
    return fail("Studio viewport should expose lasso close-on-release handling");
  }
  QCoreApplication::processEvents();
  const auto closed_lasso = editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap();
  if (editor_preview.activeMaskKind() != QStringLiteral("lasso") ||
      !closed_lasso.value(QStringLiteral("lassoClosed")).toBool() ||
      !closed_lasso.value(QStringLiteral("lassoSimplified")).toBool() ||
      !closed_lasso.value(QStringLiteral("lassoSmoothed")).toBool() ||
      closed_lasso.value(QStringLiteral("lassoRawPointCount")).toInt() != 5) {
    return fail("Studio lasso release should close, simplify, and smooth the freehand path");
  }
  QVariant lasso_finished;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "finishActiveCanvasTool",
          Q_RETURN_ARG(QVariant, lasso_finished)) ||
      !lasso_finished.toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("masks") ||
      editor_preview.activeMaskKind() != QStringLiteral("lasso")) {
    return fail("Done/Enter should finish lasso drawing and keep the lasso selection active");
  }
  editor_preview.removeActiveMask();
  QCoreApplication::processEvents();
  if (editor_preview.maskCount() != mask_count_before_polygon_cancel) {
    return fail("lasso close-on-release smoke cleanup should preserve unrelated masks");
  }
  editor_preview.startPolygonSelection(false);
  editor_preview.appendActivePolygonPoint(0.15, 0.15);
  editor_preview.appendActivePolygonPoint(0.85, 0.85);
  editor_preview.appendActivePolygonPoint(0.15, 0.85);
  editor_preview.appendActivePolygonPoint(0.85, 0.15);
  QCoreApplication::processEvents();
  QObject* polygon_self_intersection_warning = nullptr;
  if (!wait_for_condition([&]() {
        polygon_self_intersection_warning =
            find_named_object(edit_workspace, QStringLiteral("polygonSelfIntersectionWarning"));
        return polygon_self_intersection_warning != nullptr &&
               polygon_self_intersection_warning->property("visible").toBool();
      })) {
    return fail("self-intersecting polygon selections should show a warning in the UI");
  }
  if (polygon_self_intersection_warning == nullptr ||
      !polygon_self_intersection_warning->property("visible").toBool() ||
      !polygon_self_intersection_warning->property("text")
           .toString()
           .contains(QStringLiteral("Self-intersecting polygon"))) {
    return fail("self-intersecting polygon selections should show a warning in the UI");
  }
  if (polygon_hud_complete_button->property("enabled").toBool() ||
      polygon_complete_return_shortcut->property("enabled").toBool() ||
      polygon_complete_enter_shortcut->property("enabled").toBool()) {
    return fail("self-intersecting polygon selections should block completion affordances");
  }
  editor_preview.removeLastActivePolygonPoint();
  QCoreApplication::processEvents();
  if (!wait_for_condition([&]() {
        return !editor_preview.activeMaskPolygonSelfIntersecting() &&
               !polygon_self_intersection_warning->property("visible").toBool();
      })) {
    return fail("polygon self-intersection warning should clear after removing the crossing point");
  }
  editor_preview.removeActiveMask();
  QCoreApplication::processEvents();
  if (editor_preview.maskCount() != mask_count_before_polygon_cancel) {
    return fail("polygon self-intersection smoke cleanup should preserve unrelated masks");
  }
  editor_preview.setMaskPreviewMode(QStringLiteral("none"));
  QCoreApplication::processEvents();
  if (editor_preview.maskPreviewMode() != QStringLiteral("none") ||
      edit_mask_canvas_overlay->property("visible").toBool()) {
    return fail("mask preview None mode should hide active mask overlays while keeping mask state editable");
  }
  editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
  QCoreApplication::processEvents();
  editor_preview.setActiveMaskInverted(true);
  QCoreApplication::processEvents();
  if (!active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Inverted"))) {
    return fail("mask toolbar should make active source inversion visible");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskCreationOperation",
          Q_ARG(QVariant, QVariant(QStringLiteral("replace")))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "createMaskSource",
          Q_ARG(QVariant, QVariant(QStringLiteral("radial"))))) {
    return fail("edit workspace should expose replace mask creation through the shared controls");
  }
  QCoreApplication::processEvents();
  if (!mask_replace_button->property("active").toBool() ||
      editor_preview.maskCount() != initial_mask_count + 2 ||
      editor_preview.activeMaskOperation() != QStringLiteral("replace") ||
      !active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Replace"))) {
    return fail("mask creation should support an explicit replace combine mode");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskCreationOperation",
          Q_ARG(QVariant, QVariant(QStringLiteral("subtract")))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "startMaskBrushSource",
          Q_ARG(QVariant, QVariant(QStringLiteral("subtract"))))) {
    return fail("edit workspace should start brush masking through the shared mask creation controls");
  }
  QCoreApplication::processEvents();
  if (mask_subtract_button == nullptr || !mask_subtract_button->property("active").toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("brush") ||
      editor_preview.maskCount() != initial_mask_count + 3 ||
      editor_preview.activeMaskOperation() != QStringLiteral("subtract")) {
    return fail("brush mask creation should honor the selected subtract combine mode");
  }
  auto* brush_tool_hud = edit_workspace->findChild<QObject*>(QStringLiteral("brushToolHud"));
  auto* brush_add_button = edit_workspace->findChild<QObject*>(QStringLiteral("brushHudAddButton"));
  auto* brush_erase_button = edit_workspace->findChild<QObject*>(QStringLiteral("brushHudEraseButton"));
  auto* brush_done_button = edit_workspace->findChild<QObject*>(QStringLiteral("brushHudDoneButton"));
  auto* brush_status_label = edit_workspace->findChild<QObject*>(QStringLiteral("brushHudStatusLabel"));
  auto* brush_size_up_button = edit_workspace->findChild<QObject*>(QStringLiteral("brushHudSizeUpButton"));
  auto* brush_size_drag_handle =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushHudSizeDragHandle"));
  auto* brush_size_drag_area =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushHudSizeDragArea"));
  auto* brush_size_field = edit_workspace->findChild<QObject*>(QStringLiteral("brushHudSizeField"));
  auto* edit_zoom_toolbar_for_brush = find_named_object(edit_workspace, QStringLiteral("editZoomToolbar"));
  auto* edit_action_toolbar_for_brush = find_named_object(edit_workspace, QStringLiteral("editActionToolbar"));
  auto* brush_size_down_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("brushSizeDownShortcut"));
  auto* brush_size_up_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("brushSizeUpShortcut"));
  auto* brush_add_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("brushAddShortcut"));
  auto* brush_erase_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("brushEraseShortcut"));
  auto* brush_done_return_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushDoneReturnShortcut"));
  auto* brush_done_enter_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushDoneEnterShortcut"));
  auto* brush_hardness_down_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushHardnessDownShortcut"));
  auto* brush_hardness_up_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushHardnessUpShortcut"));
  auto* brush_flow_down_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushFlowDownShortcut"));
  auto* brush_flow_up_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushFlowUpShortcut"));
  auto* brush_density_down_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushDensityDownShortcut"));
  auto* brush_density_up_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushDensityUpShortcut"));
  auto* brush_opacity_down_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushOpacityDownShortcut"));
  auto* brush_opacity_up_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushOpacityUpShortcut"));
  auto* mask_create_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("maskCreateShortcut"));
  auto* mask_operation_add_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskOperationAddShortcut"));
  auto* mask_operation_subtract_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskOperationSubtractShortcut"));
  auto* mask_operation_intersect_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskOperationIntersectShortcut"));
  auto* mask_invert_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("maskInvertShortcut"));
  auto* mask_overlay_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("maskOverlayShortcut"));
  auto* mask_outline_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("maskOutlineShortcut"));
  auto* layer_target_next_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("layerTargetNextShortcut"));
  auto* layer_target_previous_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("layerTargetPreviousShortcut"));
  auto* mask_target_next_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskTargetNextShortcut"));
  auto* mask_target_previous_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskTargetPreviousShortcut"));
  auto* base_image_target_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("baseImageTargetShortcut"));
  auto* mask_select_move_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskSelectMoveShortcut"));
  auto* mask_lasso_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("maskLassoShortcut"));
  auto* mask_polygon_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("maskPolygonShortcut"));
  auto* mask_delete_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("maskDeleteShortcut"));
  auto* mask_rename_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("maskRenameShortcut"));
  auto* active_mask_rename_dialog =
      edit_workspace->findChild<QObject*>(QStringLiteral("activeMaskRenameDialog"));
  auto* brush_cursor_ring = edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportBrushCursorRing"));
  auto* brush_cursor_outer_halo_ring =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportBrushCursorOuterHaloRing"));
  auto* brush_cursor_mode_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportBrushCursorModeLabel"));
  auto* brush_canvas_overlay =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportMaskCanvasOverlay"));
  auto* brush_context_panel = edit_workspace->findChild<QObject*>(QStringLiteral("brushContextPanel"));
  auto* brush_context_layer_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextLayerNameLabel"));
  auto* brush_context_mask_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextMaskNameLabel"));
  auto* brush_context_mask_state =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextMaskStateLabel"));
  auto* brush_context_mask_chip =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextMaskChip"));
  auto* brush_context_add_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextAddButton"));
  auto* brush_context_erase_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextEraseButton"));
  auto* brush_context_overlay_toggle =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextOverlayToggle"));
  auto* brush_context_compact_panel =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextCompactBrushPanel"));
  auto* brush_context_size_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextBrushSizeLabel"));
  auto* brush_context_flow_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextBrushFlowLabel"));
  auto* brush_context_hardness_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextBrushHardnessLabel"));
  auto* brush_context_density_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextBrushDensityLabel"));
  auto* brush_context_opacity_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextBrushOpacityLabel"));
  auto* brush_context_density_down_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextDensityDownButton"));
  auto* brush_context_density_up_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextDensityUpButton"));
  auto* brush_context_opacity_down_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextOpacityDownButton"));
  auto* brush_context_opacity_up_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextOpacityUpButton"));
  auto* brush_context_exposure_slider =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextExposureSlider"));
  auto* brush_context_temperature_slider =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextTemperatureSlider"));
  QVariant brush_ready;
  QVariant active_mask_context_signature;
  QVariant active_mask_layer_context_signature;
  if (brush_tool_hud == nullptr || brush_add_button == nullptr || brush_erase_button == nullptr ||
      brush_done_button == nullptr || brush_status_label == nullptr || brush_size_up_button == nullptr ||
      brush_size_drag_handle == nullptr || brush_size_drag_area == nullptr ||
      brush_size_field == nullptr ||
      edit_zoom_toolbar_for_brush == nullptr || edit_action_toolbar_for_brush == nullptr ||
      brush_size_down_shortcut == nullptr || brush_size_up_shortcut == nullptr ||
      brush_add_shortcut == nullptr || brush_erase_shortcut == nullptr ||
      brush_done_return_shortcut == nullptr || brush_done_enter_shortcut == nullptr ||
      brush_hardness_down_shortcut == nullptr || brush_hardness_up_shortcut == nullptr ||
      brush_flow_down_shortcut == nullptr || brush_flow_up_shortcut == nullptr ||
      brush_density_down_shortcut == nullptr || brush_density_up_shortcut == nullptr ||
      brush_opacity_down_shortcut == nullptr || brush_opacity_up_shortcut == nullptr ||
      mask_create_shortcut == nullptr || mask_operation_add_shortcut == nullptr ||
      mask_operation_subtract_shortcut == nullptr || mask_operation_intersect_shortcut == nullptr ||
      mask_invert_shortcut == nullptr || mask_overlay_shortcut == nullptr ||
      mask_outline_shortcut == nullptr || layer_target_next_shortcut == nullptr ||
      layer_target_previous_shortcut == nullptr ||
      mask_target_next_shortcut == nullptr || mask_target_previous_shortcut == nullptr ||
      base_image_target_shortcut == nullptr ||
      mask_select_move_shortcut == nullptr || mask_lasso_shortcut == nullptr ||
      mask_polygon_shortcut == nullptr ||
      mask_delete_shortcut == nullptr || mask_rename_shortcut == nullptr ||
      active_mask_rename_dialog == nullptr ||
      brush_cursor_ring == nullptr || brush_cursor_outer_halo_ring == nullptr ||
      brush_cursor_mode_label == nullptr ||
      brush_canvas_overlay == nullptr || brush_context_panel == nullptr ||
      brush_context_layer_label == nullptr || brush_context_mask_label == nullptr ||
      brush_context_mask_state == nullptr || brush_context_mask_chip == nullptr ||
      brush_context_add_button == nullptr || brush_context_erase_button == nullptr ||
      brush_context_overlay_toggle == nullptr || brush_context_compact_panel == nullptr ||
      brush_context_size_label == nullptr || brush_context_flow_label == nullptr ||
      brush_context_hardness_label == nullptr || brush_context_density_label == nullptr ||
      brush_context_opacity_label == nullptr || brush_context_density_down_button == nullptr ||
      brush_context_density_up_button == nullptr || brush_context_opacity_down_button == nullptr ||
      brush_context_opacity_up_button == nullptr ||
      brush_context_exposure_slider == nullptr ||
      brush_context_temperature_slider == nullptr ||
      brush_canvas_overlay->property("outlineRenderContract").toString() !=
          QStringLiteral("anti-aliased|zoom-stable|pixel-aligned:v1") ||
      !QMetaObject::invokeMethod(edit_workspace, "brushToolReady", Q_RETURN_ARG(QVariant, brush_ready)) ||
      !brush_ready.toBool() || !brush_tool_hud->property("visible").toBool() ||
      !brush_context_panel->property("visible").toBool() ||
      brush_cursor_ring->property("brushCursorVisibilityContract").toString() !=
          QStringLiteral("persistent-last-position|dual-contrast-ring|hardness-inner-ring:v1") ||
      !brush_context_layer_label->property("text").toString().startsWith(
          QStringLiteral("Adjustment Layer")) ||
      !brush_context_mask_label->property("text").toString().contains(QStringLiteral("Brush")) ||
      !brush_context_mask_state->property("text").toString().contains(QStringLiteral("Brush selection")) ||
      !brush_context_mask_state->property("text").toString().contains(QStringLiteral("Subtract")) ||
      !brush_context_mask_state->property("text").toString().contains(QStringLiteral("Overlay")) ||
      !brush_context_mask_chip->property("visible").toBool() ||
      !brush_context_erase_button->property("active").toBool() ||
      !brush_context_overlay_toggle->property("active").toBool() ||
      brush_context_compact_panel->property("brushPanelContract").toString() !=
          QStringLiteral("size-flow-hardness-density-opacity|compact:v1") ||
      !brush_context_compact_panel->property("visible").toBool() ||
      !brush_context_size_label->property("text").toString().contains(QStringLiteral("Size")) ||
      !brush_context_flow_label->property("text").toString().contains(QStringLiteral("Flow")) ||
      !brush_context_hardness_label->property("text").toString().contains(QStringLiteral("Hard")) ||
      !brush_context_density_label->property("text").toString().contains(QStringLiteral("Density")) ||
      !brush_context_opacity_label->property("text").toString().contains(QStringLiteral("Opacity")) ||
      brush_done_button->property("text").toString() != QStringLiteral("Exit Brush") ||
      !brush_done_return_shortcut->property("enabled").toBool() ||
      !brush_done_enter_shortcut->property("enabled").toBool() ||
      !brush_size_down_shortcut->property("enabled").toBool() ||
      !brush_size_up_shortcut->property("enabled").toBool() ||
      !brush_hardness_down_shortcut->property("enabled").toBool() ||
      !brush_hardness_up_shortcut->property("enabled").toBool() ||
      !brush_flow_down_shortcut->property("enabled").toBool() ||
      !brush_flow_up_shortcut->property("enabled").toBool() ||
      !brush_density_down_shortcut->property("enabled").toBool() ||
      !brush_density_up_shortcut->property("enabled").toBool() ||
      !brush_opacity_down_shortcut->property("enabled").toBool() ||
      !brush_opacity_up_shortcut->property("enabled").toBool() ||
      !brush_add_shortcut->property("enabled").toBool() ||
      !brush_erase_shortcut->property("enabled").toBool() ||
      !mask_outline_shortcut->property("enabled").toBool() ||
      !layer_target_next_shortcut->property("enabled").toBool() ||
      !layer_target_previous_shortcut->property("enabled").toBool() ||
      !mask_target_next_shortcut->property("enabled").toBool() ||
      !mask_target_previous_shortcut->property("enabled").toBool() ||
      !base_image_target_shortcut->property("enabled").toBool() ||
      !mask_select_move_shortcut->property("enabled").toBool() ||
      !mask_lasso_shortcut->property("enabled").toBool() ||
      !mask_polygon_shortcut->property("enabled").toBool() ||
      !active_canvas_tool_chip->property("visible").toBool() ||
      active_canvas_tool_chip_name_label->property("text").toString() != QStringLiteral("Brush") ||
      !active_canvas_tool_chip_mode_label->property("text").toString().contains(QStringLiteral("Subtract")) ||
      !active_canvas_tool_done_button->property("enabled").toBool() ||
      !active_canvas_tool_cancel_button->property("enabled").toBool() ||
      !brush_context_exposure_slider->property("visible").toBool() ||
      !brush_context_temperature_slider->property("visible").toBool() ||
      !edit_workspace->property("activeCanvasToolChromeReduced").toBool() ||
      mask_creation_toolbar->property("visible").toBool() ||
      mask_stack_strip->property("visible").toBool() ||
      edit_zoom_toolbar_for_brush->property("visible").toBool() ||
      edit_action_toolbar_for_brush->property("visible").toBool() ||
      !QMetaObject::invokeMethod(
           edit_workspace,
           "activeMaskEditingContextSignature",
           Q_RETURN_ARG(QVariant, active_mask_context_signature)) ||
      active_mask_context_signature.toString() !=
          QStringLiteral("tool:brush|mask:false|brush:true|retouch:false|count:1") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "activeMaskLayerContextSignature",
          Q_RETURN_ARG(QVariant, active_mask_layer_context_signature)) ||
      !active_mask_layer_context_signature.toString().contains(
          QStringLiteral("layer:Adjustment Layer")) ||
      !active_mask_layer_context_signature.toString().contains(QStringLiteral("operation:Subtract")) ||
      !active_mask_layer_context_signature.toString().contains(QStringLiteral("preview:overlay")) ||
      !brush_erase_button->property("active").toBool()) {
    return fail("active brush masking should own the canvas HUD without generic edit or mask-stack toolbar overlap");
  }
  QVariant brush_space_pan_ready;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "maskSpacePanShortcutActive",
          Q_RETURN_ARG(QVariant, brush_space_pan_ready)) ||
      !brush_space_pan_ready.toBool()) {
    return fail("Space-pan should be available while the brush mask tool is active");
  }
  edit_workspace->setProperty("spacePanActive", true);
  QCoreApplication::processEvents();
  if (!edit_workspace->property("spacePanActive").toBool()) {
    return fail("holding Space should enter the Studio Space-pan state while masking");
  }
  edit_workspace->setProperty("spacePanActive", false);
  QCoreApplication::processEvents();
  QVariant brush_target_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activeEditTargetSignature",
          Q_RETURN_ARG(QVariant, brush_target_signature)) ||
      active_edit_target_bar->property("targetScope").toString() != QStringLiteral("mask") ||
      !active_adjustment_layer_target_bar->property("visible").toBool() ||
      active_edit_target_mode_label->property("text").toString() != QStringLiteral("Editing Mask") ||
      !active_edit_target_breadcrumb_label->property("text").toString().startsWith(
          QStringLiteral("Base Image > Adjustment Layer")) ||
      !active_edit_target_breadcrumb_label->property("text").toString().contains(QStringLiteral("Brush")) ||
      !brush_target_signature.toString().contains(QStringLiteral("scope:mask")) ||
      !brush_target_signature.toString().contains(QStringLiteral("breadcrumb:Base Image > Adjustment Layer")) ||
      !brush_target_signature.toString().contains(QStringLiteral("Brush selection")) ||
      !brush_target_signature.toString().contains(QStringLiteral("Subtract"))) {
    std::cerr << "Active brush target state: scope="
              << active_edit_target_bar->property("targetScope").toString().toStdString()
              << " barVisible=" << active_edit_target_bar->property("visible").toBool()
              << " layerProxyVisible="
              << active_adjustment_layer_target_bar->property("visible").toBool()
              << " mode="
              << active_edit_target_mode_label->property("text").toString().toStdString()
              << " breadcrumb="
              << active_edit_target_breadcrumb_label->property("text").toString().toStdString()
              << " status="
              << active_adjustment_layer_target_status_label->property("text").toString().toStdString()
              << " signature=" << brush_target_signature.toString().toStdString()
              << '\n';
    return fail("active target bar should make brush mask editing explicit with layer and mask breadcrumb");
  }
  const auto global_exposure_before_layer_adjust = editor_preview.exposure();
  const auto active_brush_adjustment_layer_index =
      edit_workspace->property("activePhotoLayerIndex").toInt();
  const auto active_brush_mask_set_id =
      editor_preview.maskStack()
          .at(editor_preview.activeMaskIndex())
          .toMap()
          .value(QStringLiteral("maskSetId"))
          .toString();
  QVariant layer_adjusted;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setPrimaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, layer_adjusted),
          Q_ARG(QVariant, QVariant(QStringLiteral("exposure"))),
          Q_ARG(QVariant, QVariant(0.31))) ||
      !layer_adjusted.toBool() ||
      editor_preview.photoLayerCount() <= 0 ||
      active_brush_adjustment_layer_index < 0 ||
      active_brush_adjustment_layer_index >= editor_preview.photoLayerCount() ||
      editor_preview.photoLayers()
              .at(active_brush_adjustment_layer_index)
              .toMap()
              .value(QStringLiteral("selectionMaskSetId"))
              .toString() != active_brush_mask_set_id ||
      std::abs(editor_preview.photoLayers()
                    .at(active_brush_adjustment_layer_index)
                    .toMap()
                    .value(QStringLiteral("exposure"))
                    .toDouble() -
                0.31) > 1e-6 ||
      std::abs(editor_preview.exposure() - global_exposure_before_layer_adjust) > 1e-6) {
    return fail("right-panel adjustment controls should edit the active adjustment layer without mutating base exposure");
  }
  if (!active_adjustment_layer_target_bar->property("visible").toBool() ||
      !active_adjustment_layer_target_name_label->property("text").toString().startsWith(
          QStringLiteral("Adjustment Layer")) ||
      !active_adjustment_layer_target_status_label->property("text").toString().contains(
          QStringLiteral("Brush selection")) ||
      !active_adjustment_layer_open_layers_button->property("visible").toBool()) {
    return fail("adjust inspector should show the active brush mask and linked adjustment layer as the current right-panel target");
  }
  editor_preview.setTemperature(27.0);
  QCoreApplication::processEvents();
  QVariant layer_temperature_value;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "primaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, layer_temperature_value),
          Q_ARG(QVariant, QVariant(QStringLiteral("temperature"))),
          Q_ARG(QVariant, QVariant(editor_preview.temperature()))) ||
      std::abs(layer_temperature_value.toDouble()) > 1e-6) {
    return fail("neutral active adjustment layers should not display inherited base white balance values");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setPrimaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, layer_adjusted),
          Q_ARG(QVariant, QVariant(QStringLiteral("temperature"))),
          Q_ARG(QVariant, QVariant(-16.0))) ||
      !layer_adjusted.toBool() ||
      std::abs(editor_preview.photoLayers()
                    .at(active_brush_adjustment_layer_index)
                    .toMap()
                    .value(QStringLiteral("temperature"))
                    .toDouble() +
               16.0) > 1e-6 ||
      std::abs(editor_preview.temperature() - 27.0) > 1e-6) {
    return fail("right-panel white balance controls should edit the active layer without mutating base temperature");
  }
  QVariant layer_reset;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "resetPrimaryAdjustmentGroup",
          Q_RETURN_ARG(QVariant, layer_reset),
          Q_ARG(QVariant, QVariant(QStringLiteral("whiteBalance")))) ||
      !layer_reset.toBool() ||
      editor_preview.photoLayers()
          .at(active_brush_adjustment_layer_index)
          .toMap()
          .contains(QStringLiteral("temperature")) ||
      std::abs(editor_preview.temperature() - 27.0) > 1e-6) {
    return fail("right-panel panel reset should clear active-layer white balance without mutating base temperature");
  }
  editor_preview.setTemperature(0.0);
  editor_preview.setTexture(58.0);
  editor_preview.setSharpenRadius(2.4);
  editor_preview.setLensDistortion(-18.0);
  editor_preview.setTransformScale(1.7);
  editor_preview.setContentAwareCropFill(true);
  QCoreApplication::processEvents();
  QVariant neutral_layer_texture;
  QVariant neutral_layer_sharpen_radius;
  QVariant detail_layer_adjusted;
  QVariant detail_layer_bool_adjusted;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "primaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, neutral_layer_texture),
          Q_ARG(QVariant, QVariant(QStringLiteral("texture"))),
          Q_ARG(QVariant, QVariant(editor_preview.texture()))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "primaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, neutral_layer_sharpen_radius),
          Q_ARG(QVariant, QVariant(QStringLiteral("sharpenRadius"))),
          Q_ARG(QVariant, QVariant(editor_preview.sharpenRadius()))) ||
      std::abs(neutral_layer_texture.toDouble()) > 1e-6 ||
      std::abs(neutral_layer_sharpen_radius.toDouble() - 1.0) > 1e-6) {
    return fail("neutral active adjustment layers should not display inherited base detail values");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setPrimaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, detail_layer_adjusted),
          Q_ARG(QVariant, QVariant(QStringLiteral("texture"))),
          Q_ARG(QVariant, QVariant(24.0))) ||
      !detail_layer_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setPrimaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, detail_layer_adjusted),
          Q_ARG(QVariant, QVariant(QStringLiteral("sharpenRadius"))),
          Q_ARG(QVariant, QVariant(1.6))) ||
      !detail_layer_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setPrimaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, detail_layer_adjusted),
          Q_ARG(QVariant, QVariant(QStringLiteral("lensDistortion"))),
          Q_ARG(QVariant, QVariant(-33.0))) ||
      !detail_layer_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setPrimaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, detail_layer_adjusted),
          Q_ARG(QVariant, QVariant(QStringLiteral("transformScale"))),
          Q_ARG(QVariant, QVariant(1.35))) ||
      !detail_layer_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setPrimaryBoolAdjustmentValue",
          Q_RETURN_ARG(QVariant, detail_layer_bool_adjusted),
          Q_ARG(QVariant, QVariant(QStringLiteral("contentAwareCropFill"))),
          Q_ARG(QVariant, QVariant(false))) ||
      !detail_layer_bool_adjusted.toBool()) {
    return fail("target-aware detail, lens, and transform controls should accept active layer writes");
  }
  auto layer_local_detail = editor_preview.photoLayers().at(active_brush_adjustment_layer_index).toMap();
  if (std::abs(layer_local_detail.value(QStringLiteral("texture")).toDouble() - 24.0) > 1e-6 ||
      std::abs(layer_local_detail.value(QStringLiteral("sharpenRadius")).toDouble() - 1.6) > 1e-6 ||
      std::abs(layer_local_detail.value(QStringLiteral("lensDistortion")).toDouble() + 33.0) > 1e-6 ||
      std::abs(layer_local_detail.value(QStringLiteral("transformScale")).toDouble() - 1.35) > 1e-6 ||
      layer_local_detail.value(QStringLiteral("contentAwareCropFill")).toBool() ||
      std::abs(editor_preview.texture() - 58.0) > 1e-6 ||
      std::abs(editor_preview.sharpenRadius() - 2.4) > 1e-6 ||
      std::abs(editor_preview.lensDistortion() + 18.0) > 1e-6 ||
      std::abs(editor_preview.transformScale() - 1.7) > 1e-6 ||
      !editor_preview.contentAwareCropFill()) {
    return fail("active layer detail/lens/transform writes should not mutate Base Image values");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "resetPrimaryAdjustmentGroup",
          Q_RETURN_ARG(QVariant, layer_reset),
          Q_ARG(QVariant, QVariant(QStringLiteral("presence")))) ||
      !layer_reset.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "resetPrimaryAdjustmentGroup",
          Q_RETURN_ARG(QVariant, layer_reset),
          Q_ARG(QVariant, QVariant(QStringLiteral("lens")))) ||
      !layer_reset.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "resetPrimaryAdjustmentGroup",
          Q_RETURN_ARG(QVariant, layer_reset),
          Q_ARG(QVariant, QVariant(QStringLiteral("transform")))) ||
      !layer_reset.toBool()) {
    return fail("target-aware panel resets should clear active-layer presence, lens, and transform groups");
  }
  layer_local_detail = editor_preview.photoLayers().at(active_brush_adjustment_layer_index).toMap();
  if (layer_local_detail.contains(QStringLiteral("texture")) ||
      layer_local_detail.contains(QStringLiteral("lensDistortion")) ||
      layer_local_detail.contains(QStringLiteral("transformScale")) ||
      layer_local_detail.contains(QStringLiteral("contentAwareCropFill")) ||
      std::abs(editor_preview.texture() - 58.0) > 1e-6 ||
      std::abs(editor_preview.lensDistortion() + 18.0) > 1e-6 ||
      std::abs(editor_preview.transformScale() - 1.7) > 1e-6 ||
      !editor_preview.contentAwareCropFill()) {
    return fail("target-aware panel resets should leave Base Image detail/lens/transform state untouched");
  }
  editor_preview.setTexture(0.0);
  editor_preview.setSharpenRadius(1.0);
  editor_preview.setLensDistortion(0.0);
  editor_preview.setTransformScale(1.0);
  editor_preview.setContentAwareCropFill(false);
  QCoreApplication::processEvents();
  const auto mask_count_before_brush_exit = editor_preview.maskCount();
  QVariant brush_finished;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "finishActiveCanvasTool",
          Q_RETURN_ARG(QVariant, brush_finished)) ||
      !brush_finished.toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("masks") ||
      !editor_preview.hasActiveMask() ||
      editor_preview.activeMaskKind() != QStringLiteral("brush") ||
      editor_preview.maskCount() != mask_count_before_brush_exit ||
      active_canvas_tool_chip->property("visible").toBool() ||
      brush_tool_hud->property("visible").toBool()) {
    return fail("Done/Enter should exit brush mode while keeping the active brush selection selected");
  }
  edit_workspace->setProperty("editTool", QStringLiteral("brush"));
  QCoreApplication::processEvents();
  QVariant brush_cancelled;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "cancelActiveTool",
          Q_RETURN_ARG(QVariant, brush_cancelled)) ||
      !brush_cancelled.toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("masks") ||
      !editor_preview.hasActiveMask() ||
      editor_preview.activeMaskKind() != QStringLiteral("brush") ||
      editor_preview.maskCount() != mask_count_before_brush_exit ||
      active_canvas_tool_chip->property("visible").toBool() ||
      brush_tool_hud->property("visible").toBool()) {
    return fail("Escape should exit brush mode without deleting or deselecting the active brush selection");
  }
  const auto mask_count_before_shortcut_create = editor_preview.maskCount();
  const auto inverted_before_shortcut = editor_preview.activeMaskSelectionInverted();
  QVariant mask_shortcut_signature;
  QVariant mask_shortcut_created;
  QVariant mask_shortcut_add;
  QVariant mask_shortcut_subtract;
  QVariant mask_shortcut_intersect;
  QVariant mask_shortcut_inverted;
  QVariant mask_shortcut_overlay_hidden;
  QVariant mask_shortcut_overlay_shown;
  QVariant mask_shortcut_outline_enabled;
  QVariant mask_shortcut_outline_disabled;
  QVariant mask_shortcut_layer_target;
  QVariant mask_shortcut_layer_target_previous;
  QVariant mask_shortcut_mask_target;
  QVariant mask_shortcut_mask_target_previous;
  QVariant mask_shortcut_base_target;
  QVariant mask_shortcut_base_scope;
  QVariant mask_shortcut_mask_scope;
  QVariant mask_selected_target_contract;
  QVariant mask_shortcut_overlay_temporarily_hidden;
  QVariant mask_shortcut_overlay_temporarily_shown;
  QVariant mask_shortcut_rename_opened;
  QVariant mask_global_write_guard_signature;
  QVariant mask_global_reset_blocked;
  QVariant mask_shortcut_create_blocked_by_text_input;
  QVariant mask_shortcut_deleted;
  QVariant mask_shortcut_select_move;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "maskShortcutSignature",
          Q_RETURN_ARG(QVariant, mask_shortcut_signature)) ||
      mask_shortcut_signature.toString() !=
          QStringLiteral(
              "select:V|brush:B|erase:E|lasso:L|polygon:P|create:Ctrl+Shift+M|add:Ctrl+Shift+A|subtract:Ctrl+Shift+S|intersect:Ctrl+Shift+I|invert:Ctrl+Shift+V|overlay:Ctrl+Shift+O|outline:Ctrl+Shift+U|layer-target:Ctrl+Shift+L|layer-target-prev:Ctrl+Alt+Shift+L|mask-target:Ctrl+Shift+K|mask-target-prev:Ctrl+Alt+Shift+K|base-image:Ctrl+Shift+B|temp-erase:X-hold|temporary-hide:H-hold|delete:Delete|rename:F2") ||
      !mask_create_shortcut->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "selectMaskMoveFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_select_move)) ||
      !mask_shortcut_select_move.toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("masks") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "createMaskFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_created)) ||
      !mask_shortcut_created.toBool() ||
      editor_preview.maskCount() != mask_count_before_shortcut_create + 1 ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskOperationFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_add),
          Q_ARG(QVariant, QStringLiteral("add"))) ||
      !mask_shortcut_add.toBool() || editor_preview.activeMaskOperation() != QStringLiteral("add") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskOperationFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_subtract),
          Q_ARG(QVariant, QStringLiteral("subtract"))) ||
      !mask_shortcut_subtract.toBool() ||
      editor_preview.activeMaskOperation() != QStringLiteral("subtract") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskOperationFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_intersect),
          Q_ARG(QVariant, QStringLiteral("intersect"))) ||
      !mask_shortcut_intersect.toBool() ||
      editor_preview.activeMaskOperation() != QStringLiteral("intersect") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "invertActiveMaskFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_inverted)) ||
      !mask_shortcut_inverted.toBool() ||
      editor_preview.activeMaskSelectionInverted() == inverted_before_shortcut ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "toggleActiveMaskOverlayFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_overlay_hidden)) ||
      !mask_shortcut_overlay_hidden.toBool() ||
      editor_preview.maskPreviewMode() != QStringLiteral("none") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "toggleActiveMaskOverlayFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_overlay_shown)) ||
      !mask_shortcut_overlay_shown.toBool() ||
      editor_preview.maskPreviewMode() != QStringLiteral("overlay") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "toggleMaskOutlineFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_outline_enabled)) ||
      !mask_shortcut_outline_enabled.toBool() ||
      editor_preview.maskPreviewMode() != QStringLiteral("outline") ||
      editor_preview.maskPreviewDisplayMode() != QStringLiteral("outline") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "toggleMaskOutlineFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_outline_disabled)) ||
      !mask_shortcut_outline_disabled.toBool() ||
      editor_preview.maskPreviewMode() != QStringLiteral("overlay") ||
      editor_preview.maskPreviewDisplayMode() != QStringLiteral("overlay") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "switchLayerTargetFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_layer_target),
          Q_ARG(QVariant, 1)) ||
      !mask_shortcut_layer_target.toBool() ||
      edit_workspace->property("activePhotoLayerIndex").toInt() < 0 ||
      edit_workspace->property("activePhotoLayerIndex").toInt() >= editor_preview.photoLayerCount() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "switchLayerTargetFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_layer_target_previous),
          Q_ARG(QVariant, -1)) ||
      !mask_shortcut_layer_target_previous.toBool() ||
      edit_workspace->property("activePhotoLayerIndex").toInt() < 0 ||
      edit_workspace->property("activePhotoLayerIndex").toInt() >= editor_preview.photoLayerCount() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "returnToBaseImageFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_base_target)) ||
      !mask_shortcut_base_target.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "activeEditTargetScope",
          Q_RETURN_ARG(QVariant, mask_shortcut_base_scope)) ||
      mask_shortcut_base_scope.toString() != QStringLiteral("base-image") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "switchMaskTargetFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_mask_target),
          Q_ARG(QVariant, 1)) ||
      !mask_shortcut_mask_target.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "activeEditTargetScope",
          Q_RETURN_ARG(QVariant, mask_shortcut_mask_scope)) ||
      mask_shortcut_mask_scope.toString() != QStringLiteral("mask") ||
      (mask_selected_target_contract =
           edit_workspace->property("selectedEditTargetContract")).toString().isEmpty() ||
      !mask_selected_target_contract.toString().contains(QStringLiteral("selected-target:v1|scope:mask")) ||
      !mask_selected_target_contract.toString().contains(QStringLiteral("|layer:")) ||
      !mask_selected_target_contract.toString().contains(QStringLiteral("|mask:")) ||
      !mask_selected_target_contract.toString().contains(QStringLiteral("|base:false")) ||
      !editor_preview.hasActiveMask() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "switchMaskTargetFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_mask_target_previous),
          Q_ARG(QVariant, -1)) ||
      !mask_shortcut_mask_target_previous.toBool() ||
      !editor_preview.hasActiveMask() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskPreviewTemporarilyHiddenFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_overlay_temporarily_hidden),
          Q_ARG(QVariant, true)) ||
      !mask_shortcut_overlay_temporarily_hidden.toBool() ||
      !editor_preview.maskPreviewTemporarilyHidden() ||
      editor_preview.maskPreviewMode() != QStringLiteral("overlay") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskPreviewTemporarilyHiddenFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_overlay_temporarily_shown),
          Q_ARG(QVariant, false)) ||
      !mask_shortcut_overlay_temporarily_shown.toBool() ||
      editor_preview.maskPreviewTemporarilyHidden() ||
      editor_preview.maskPreviewMode() != QStringLiteral("overlay") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "renameActiveMaskFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_rename_opened)) ||
      !mask_shortcut_rename_opened.toBool() ||
      !active_mask_rename_dialog->property("opened").toBool() ||
      edit_workspace->property("activeMaskRenameDraft").toString() != editor_preview.activeMaskName()) {
    return fail("mask keyboard shortcuts should create, combine, invert, preview, temporarily hide preview, and open rename for active selections");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "globalAdjustmentWriteGuardSignature",
          Q_RETURN_ARG(QVariant, mask_global_write_guard_signature)) ||
      !mask_global_write_guard_signature.toString().contains(
          QStringLiteral("global-write-guard:v1")) ||
      !mask_global_write_guard_signature.toString().contains(QStringLiteral("scope:mask")) ||
      !mask_global_write_guard_signature.toString().contains(QStringLiteral("selected-target:v1|scope:mask")) ||
      !mask_global_write_guard_signature.toString().contains(QStringLiteral("allowed:false")) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "resetPrimaryAdjustmentGroup",
          Q_RETURN_ARG(QVariant, mask_global_reset_blocked),
          Q_ARG(QVariant, QVariant(QStringLiteral("raw")))) ||
      mask_global_reset_blocked.toBool()) {
    std::cerr << "Mask global write guard: signature="
              << mask_global_write_guard_signature.toString().toStdString()
              << " rawResetResult=" << mask_global_reset_blocked.toBool() << '\n';
    return fail("mask/layer-local targets should block global-only adjustment writes until Base Image is explicitly active");
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  if (mask_create_shortcut->property("enabled").toBool() ||
      mask_outline_shortcut->property("enabled").toBool() ||
      layer_target_next_shortcut->property("enabled").toBool() ||
      brush_size_up_shortcut->property("enabled").toBool() ||
      brush_done_return_shortcut->property("enabled").toBool() ||
      brush_add_shortcut->property("enabled").toBool() ||
      brush_erase_shortcut->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "createMaskFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_create_blocked_by_text_input)) ||
      mask_shortcut_create_blocked_by_text_input.toBool()) {
    return fail("mask and brush shortcuts should be disabled while a mask rename text field is active");
  }
  QMetaObject::invokeMethod(active_mask_rename_dialog, "close");
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  if (active_mask_rename_dialog->property("opened").toBool() ||
      !mask_create_shortcut->property("enabled").toBool() ||
      !mask_outline_shortcut->property("enabled").toBool() ||
      !brush_add_shortcut->property("enabled").toBool() ||
      !brush_erase_shortcut->property("enabled").toBool()) {
    return fail("mask and brush shortcuts should re-enable after closing mask rename text input");
  }
  QVariant mask_modifier_default;
  QVariant mask_modifier_add;
  QVariant mask_modifier_subtract;
  QVariant mask_modifier_intersect;
  QVariant mask_modifier_applied;
  QVariant object_modifier_add_mode;
  QVariant object_modifier_subtract_mode;
  const auto shift_modifier = static_cast<int>(Qt::ShiftModifier);
  const auto option_modifier = static_cast<int>(Qt::AltModifier);
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskCreationOperation",
          Q_ARG(QVariant, QVariant(QStringLiteral("replace")))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskOperationForModifierState",
          Q_RETURN_ARG(QVariant, mask_modifier_default),
          Q_ARG(QVariant, QVariant(0))) ||
      mask_modifier_default.toString() != QStringLiteral("replace") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskOperationForModifierState",
          Q_RETURN_ARG(QVariant, mask_modifier_add),
          Q_ARG(QVariant, QVariant(shift_modifier))) ||
      mask_modifier_add.toString() != QStringLiteral("add") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskOperationForModifierState",
          Q_RETURN_ARG(QVariant, mask_modifier_subtract),
          Q_ARG(QVariant, QVariant(option_modifier))) ||
      mask_modifier_subtract.toString() != QStringLiteral("subtract") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskOperationForModifierState",
          Q_RETURN_ARG(QVariant, mask_modifier_intersect),
          Q_ARG(QVariant, QVariant(shift_modifier | option_modifier))) ||
      mask_modifier_intersect.toString() != QStringLiteral("intersect") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "applyMaskOperationForModifierState",
          Q_RETURN_ARG(QVariant, mask_modifier_applied),
          Q_ARG(QVariant, QVariant(shift_modifier | option_modifier))) ||
      mask_modifier_applied.toString() != QStringLiteral("intersect") ||
      editor_preview.activeMaskOperation() != QStringLiteral("intersect") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "objectPickModeForModifierState",
          Q_RETURN_ARG(QVariant, object_modifier_add_mode),
          Q_ARG(QVariant, QVariant(shift_modifier))) ||
      object_modifier_add_mode.toString() != QStringLiteral("selection") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "objectPickModeForModifierState",
          Q_RETURN_ARG(QVariant, object_modifier_subtract_mode),
          Q_ARG(QVariant, QVariant(option_modifier))) ||
      object_modifier_subtract_mode.toString() != QStringLiteral("selection-subtract")) {
    return fail("mask draw modifiers should map Shift to Add, Option to Subtract, and Shift+Option to Intersect");
  }
  if (editor_preview.activeMaskIndex() < 0 ||
      editor_preview.activeMaskIndex() >= editor_preview.maskStack().size()) {
    std::cerr << "Shortcut-created mask state: activeMaskIndex="
              << editor_preview.activeMaskIndex()
              << " maskStackSize=" << editor_preview.maskStack().size()
              << " maskCount=" << editor_preview.maskCount() << '\n';
    return fail("creating a selection from the QML shortcut path should leave a valid active mask");
  }
  const auto shortcut_mask_set_id =
      editor_preview.maskStack()
          .at(editor_preview.activeMaskIndex())
          .toMap()
          .value(QStringLiteral("maskSetId"))
          .toString();
  auto shortcut_linked_layer_index = -1;
  for (auto layer_index = 0; layer_index < editor_preview.photoLayers().size(); ++layer_index) {
    const auto layer = editor_preview.photoLayers().at(layer_index).toMap();
    if (layer.value(QStringLiteral("selectionMaskSetId")).toString() == shortcut_mask_set_id) {
      shortcut_linked_layer_index = layer_index;
      break;
    }
  }
  if (shortcut_linked_layer_index < 0 ||
      edit_workspace->property("activePhotoLayerIndex").toInt() != shortcut_linked_layer_index) {
    return fail("creating a selection from the QML shortcut path should select its linked adjustment layer target");
  }
  QMetaObject::invokeMethod(active_mask_rename_dialog, "close");
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "deleteActiveMaskFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_deleted)) ||
      !mask_shortcut_deleted.toBool() ||
      editor_preview.maskCount() != mask_count_before_shortcut_create) {
    return fail("mask keyboard shortcut delete should remove the active selection created by the create shortcut");
  }
  QVariant base_return_layer_selected;
  QVariant base_return_scope;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, base_return_layer_selected),
          Q_ARG(QVariant, QVariant(shortcut_linked_layer_index)),
          Q_ARG(QVariant, QVariant(false))) ||
      !base_return_layer_selected.toBool()) {
    return fail("Return to Base Image smoke setup should select a concrete layer target first");
  }
  process_layout_events(4);
  auto* active_edit_target_return_to_base_button =
      find_named_object(edit_workspace, QStringLiteral("activeEditTargetReturnToBaseButton"));
  if (active_edit_target_return_to_base_button == nullptr ||
      !active_edit_target_return_to_base_button->property("visible").toBool() ||
      !QMetaObject::invokeMethod(active_edit_target_return_to_base_button, "click")) {
    return fail("Return to Base Image button should be visible and clickable while editing a layer target");
  }
  process_layout_events(4);
  const auto selected_layers_after_base_return =
      edit_workspace->property("selectedPhotoLayerIndices").toList();
  auto base_return_kept_selected_layer = false;
  for (const auto& selected_layer : selected_layers_after_base_return) {
    if (selected_layer.toInt() == shortcut_linked_layer_index) {
      base_return_kept_selected_layer = true;
      break;
    }
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activeEditTargetScope",
          Q_RETURN_ARG(QVariant, base_return_scope)) ||
      base_return_scope.toString() != QStringLiteral("base-image") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
      edit_workspace->property("editInspectorTab").toString() != QStringLiteral("adjust") ||
      edit_workspace->property("activePhotoLayerIndex").toInt() != shortcut_linked_layer_index ||
      !base_return_kept_selected_layer) {
    std::cerr << "Return to Base Image state: scope="
              << base_return_scope.toString().toStdString()
              << " tool=" << edit_workspace->property("editTool").toString().toStdString()
              << " tab=" << edit_workspace->property("editInspectorTab").toString().toStdString()
              << " activeLayer=" << edit_workspace->property("activePhotoLayerIndex").toInt()
              << " expectedLayer=" << shortcut_linked_layer_index
              << " selectedCount=" << selected_layers_after_base_return.size() << '\n';
    return fail("Return to Base Image should exit layer editing while preserving the selected layer");
  }
  if (editor_preview.maskCount() > 0 &&
      (editor_preview.activeMaskIndex() < 0 ||
       editor_preview.activeMaskIndex() >= editor_preview.maskStack().size())) {
    editor_preview.setActiveMaskIndex(0);
    process_layout_events(2);
  }
  QVariant base_return_mask_target_restored;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "switchMaskTargetFromShortcut",
          Q_RETURN_ARG(QVariant, base_return_mask_target_restored),
          Q_ARG(QVariant, 1)) ||
      !base_return_mask_target_restored.toBool() ||
      !editor_preview.hasActiveMask()) {
    return fail("Return to Base Image proof should restore a valid mask target before continuing brush checks");
  }
  process_layout_events(2);
  editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
  editor_preview.setActiveMaskOperation(QStringLiteral("subtract"));
  editor_preview.setActiveMaskSelectionInverted(false);
  edit_workspace->setProperty("editTool", QStringLiteral("brush"));
  QCoreApplication::processEvents();
  QVariant hidden_preview_mode;
  QVariant exposure_adjusted;
  QVariant temperature_adjusted;
  const auto brush_quick_adjustment_set_id =
      editor_preview.maskStack()
          .at(editor_preview.activeMaskIndex())
          .toMap()
          .value(QStringLiteral("maskSetId"))
          .toString();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActiveMaskPreviewVisible",
          Q_RETURN_ARG(QVariant, hidden_preview_mode),
          Q_ARG(QVariant, false)) ||
      hidden_preview_mode.toString() != QStringLiteral("hidden") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setActiveMaskQuickAdjustment",
          Q_RETURN_ARG(QVariant, exposure_adjusted),
          Q_ARG(QVariant, QStringLiteral("exposure")),
          Q_ARG(QVariant, 1.25)) ||
      !exposure_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setActiveMaskQuickAdjustment",
          Q_RETURN_ARG(QVariant, temperature_adjusted),
          Q_ARG(QVariant, QStringLiteral("temperature")),
          Q_ARG(QVariant, -34.0)) ||
      !temperature_adjusted.toBool() ||
      editor_preview.maskPreviewMode() != QStringLiteral("none") ||
      editor_preview.maskPreviewDisplayMode() != QStringLiteral("hidden") ||
      editor_preview.photoLayerCount() <= 0 ||
      std::abs(editor_preview.activeMaskExposure()) > 1e-6 ||
      std::abs(editor_preview.activeMaskTemperature()) > 1e-6) {
    return fail("brush context panel should toggle mask preview and edit the linked adjustment layer");
  }
  QVariantMap brush_quick_adjustment_layer;
  for (const auto& layer_value : editor_preview.photoLayers()) {
    const auto layer = layer_value.toMap();
    if (layer.value(QStringLiteral("selectionMaskSetId")).toString() ==
        brush_quick_adjustment_set_id) {
      brush_quick_adjustment_layer = layer;
      break;
    }
  }
  if (brush_quick_adjustment_layer.isEmpty() ||
      std::abs(brush_quick_adjustment_layer.value(QStringLiteral("exposure")).toDouble() -
               1.25) > 1e-6 ||
      std::abs(brush_quick_adjustment_layer.value(QStringLiteral("temperature")).toDouble() +
               34.0) > 1e-6) {
    return fail("brush context panel should write quick edits to the active mask's linked adjustment layer");
  }
  editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
  QCoreApplication::processEvents();
  const auto brush_size_before = editor_preview.activeMaskBrushSize();
  const auto brush_flow_before = editor_preview.activeMaskBrushFlow();
  const auto brush_hardness_before = editor_preview.activeMaskBrushHardness();
  const auto brush_density_before = editor_preview.activeMaskDensity();
  const auto brush_opacity_before = editor_preview.activeMaskOpacity();
  QVariant brush_size_adjusted;
  QVariant brush_size_percent_set;
  QVariant brush_size_text_set;
  QVariant brush_size_drag_started;
  QVariant brush_size_drag_updated;
  QVariant brush_size_drag_ended;
  QVariant brush_flow_adjusted;
  QVariant brush_hardness_adjusted;
  QVariant brush_density_adjusted;
  QVariant brush_opacity_adjusted;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveBrushSize",
          Q_RETURN_ARG(QVariant, brush_size_adjusted),
          Q_ARG(QVariant, 0.02)) ||
      !brush_size_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveBrushFlow",
          Q_RETURN_ARG(QVariant, brush_flow_adjusted),
          Q_ARG(QVariant, -0.05)) ||
      !brush_flow_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveBrushHardness",
          Q_RETURN_ARG(QVariant, brush_hardness_adjusted),
          Q_ARG(QVariant, 0.08)) ||
      !brush_hardness_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveMaskDensity",
          Q_RETURN_ARG(QVariant, brush_density_adjusted),
          Q_ARG(QVariant, -0.05)) ||
      !brush_density_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveMaskOpacity",
          Q_RETURN_ARG(QVariant, brush_opacity_adjusted),
          Q_ARG(QVariant, -0.05)) ||
      !brush_opacity_adjusted.toBool() ||
      editor_preview.activeMaskBrushSize() <= brush_size_before ||
      editor_preview.activeMaskBrushFlow() >= brush_flow_before ||
      editor_preview.activeMaskBrushHardness() <= brush_hardness_before ||
      editor_preview.activeMaskDensity() >= brush_density_before ||
      editor_preview.activeMaskOpacity() >= brush_opacity_before) {
    return fail("brush HUD adjustment functions should update size, flow, hardness, density, and opacity");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActiveBrushSizePercent",
          Q_RETURN_ARG(QVariant, brush_size_percent_set),
          Q_ARG(QVariant, 18.0)) ||
      !brush_size_percent_set.toBool()) {
    return fail("brush HUD should expose exact percent entry for brush size");
  }
  QCoreApplication::processEvents();
  if (std::abs(editor_preview.activeMaskBrushSize() - 0.18) > 1e-6 ||
      brush_size_field->property("text").toString() != QStringLiteral("18")) {
    return fail("brush HUD numeric size field should reflect exact stored radius percentage");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActiveBrushSizeText",
          Q_RETURN_ARG(QVariant, brush_size_text_set),
          Q_ARG(QVariant, QStringLiteral("7%"))) ||
      !brush_size_text_set.toBool() ||
      std::abs(editor_preview.activeMaskBrushSize() - 0.07) > 1e-6) {
    return fail("brush HUD numeric size field should parse percentage text");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "beginBrushHudSizeDrag",
          Q_RETURN_ARG(QVariant, brush_size_drag_started),
          Q_ARG(QVariant, 100.0)) ||
      !brush_size_drag_started.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "updateBrushHudSizeDrag",
          Q_RETURN_ARG(QVariant, brush_size_drag_updated),
          Q_ARG(QVariant, 140.0)) ||
      !brush_size_drag_updated.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "endBrushHudSizeDrag",
          Q_RETURN_ARG(QVariant, brush_size_drag_ended)) ||
      !brush_size_drag_ended.toBool() ||
      editor_preview.activeMaskBrushSize() <= 0.07) {
    return fail("brush HUD drag handle should resize the active brush");
  }
  editor_preview.setActiveMaskBrushSpacing(0.42);
  editor_preview.setActiveMaskBrushSmoothing(0.36);
  editor_preview.setActiveMaskBrushPressureSize(0.73);
  editor_preview.setActiveMaskBrushPressureFlow(0.64);
  editor_preview.setActiveMaskBrushPressureOpacity(0.55);
  QCoreApplication::processEvents();
  const auto brush_size_text =
      QStringLiteral("Size %1").arg(editor_preview.activeMaskBrushSizeSummary());
  const auto brush_flow_text = QStringLiteral("Flow %1%").arg(
      static_cast<int>(std::lround(editor_preview.activeMaskBrushFlow() * 100.0)));
  const auto brush_hardness_text = QStringLiteral("Hard %1%").arg(
      static_cast<int>(std::lround(editor_preview.activeMaskBrushHardness() * 100.0)));
  const auto brush_density_text = QStringLiteral("Density %1%").arg(
      static_cast<int>(std::lround(editor_preview.activeMaskDensity() * 100.0)));
  const auto brush_opacity_text = QStringLiteral("Opacity %1%").arg(
      static_cast<int>(std::lround(editor_preview.activeMaskOpacity() * 100.0)));
  const auto brush_spacing_text = QStringLiteral("Spacing %1% of diameter").arg(
      static_cast<int>(std::lround(editor_preview.activeMaskBrushSpacing() * 100.0)));
  const auto brush_smoothing_text = QStringLiteral("Smooth %1%").arg(
      static_cast<int>(std::lround(editor_preview.activeMaskBrushSmoothing() * 100.0)));
  const auto brush_pressure_text =
      QStringLiteral("Pressure S/F/O %1/%2/%3")
          .arg(static_cast<int>(std::lround(editor_preview.activeMaskBrushPressureSize() * 100.0)))
          .arg(static_cast<int>(std::lround(editor_preview.activeMaskBrushPressureFlow() * 100.0)))
          .arg(static_cast<int>(
              std::lround(editor_preview.activeMaskBrushPressureOpacity() * 100.0)));
  const auto expected_brush_value_signature = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
                                                   .arg(brush_size_text)
                                                   .arg(brush_flow_text)
                                                   .arg(brush_hardness_text)
                                                   .arg(brush_density_text)
                                                   .arg(brush_opacity_text)
                                                   .arg(brush_spacing_text)
                                                   .arg(brush_smoothing_text)
                                                   .arg(brush_pressure_text);
  QVariant brush_value_signature;
  const auto brush_status_text = brush_status_label->property("text").toString();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "brushHudValueSignature",
          Q_RETURN_ARG(QVariant, brush_value_signature)) ||
      brush_value_signature.toString() != expected_brush_value_signature ||
      !brush_status_text.contains(brush_size_text) ||
      !brush_status_text.contains(brush_flow_text) ||
      !brush_status_text.contains(brush_hardness_text) ||
      !brush_status_text.contains(brush_density_text) ||
      !brush_status_text.contains(brush_opacity_text) ||
      !brush_context_density_label->property("text").toString().contains(brush_density_text) ||
      !brush_context_opacity_label->property("text").toString().contains(brush_opacity_text) ||
      !brush_status_text.contains(brush_spacing_text) ||
      !brush_status_text.contains(brush_smoothing_text) ||
      !brush_status_text.contains(brush_pressure_text) ||
      brush_cursor_ring->property("brushDiameter").toDouble() <= 0.0 ||
      brush_cursor_ring->property("hardnessDiameter").toDouble() <= 0.0 ||
      brush_cursor_ring->property("hardnessDiameter").toDouble() >
          brush_cursor_ring->property("brushDiameter").toDouble() ||
      !brush_cursor_mode_label->property("text").toString().contains(brush_size_text.mid(5)) ||
      !brush_cursor_mode_label->property("text").toString().contains(brush_hardness_text) ||
      brush_cursor_ring->property("brushModeLabel").toString() != QStringLiteral("Erase")) {
    return fail("brush HUD and context panel should expose true size, flow, hardness, density, opacity, spacing, smoothing, pressure response, and active mode");
  }
  editor_preview.beginActiveBrushStroke();
  editor_preview.addBrushPointWithTablet(0.42, 0.52, 0.64, 0.35, -0.20);
  QCoreApplication::processEvents();
  const auto live_tablet_status_text = brush_status_label->property("text").toString();
  if (!live_tablet_status_text.contains(QStringLiteral("Pressure 64%")) ||
      live_tablet_status_text.contains(QStringLiteral("Tilt")) ||
      brush_cursor_ring->property("pressureText").toString() != QStringLiteral("Pressure 64%") ||
      !brush_canvas_overlay->property("activeBrushPainting").toBool() ||
      !brush_canvas_overlay->property("fullMaskMatteVisible").toBool()) {
    return fail("brush HUD, cursor, and canvas overlay should expose live tablet pressure plus a visible live matte without unsupported tilt status");
  }
  editor_preview.endActiveBrushStroke();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  const auto brush_size_before_shortcut = editor_preview.activeMaskBrushSize();
  const auto brush_hardness_before_shortcut = editor_preview.activeMaskBrushHardness();
  const auto brush_flow_before_shortcut = editor_preview.activeMaskBrushFlow();
  const auto brush_density_before_shortcut = editor_preview.activeMaskDensity();
  const auto brush_opacity_before_shortcut = editor_preview.activeMaskOpacity();
  QVariant brush_size_shortcut_adjusted;
  QVariant brush_hardness_shortcut_adjusted;
  QVariant brush_flow_shortcut_adjusted_down;
  QVariant brush_flow_shortcut_adjusted_up;
  QVariant brush_density_shortcut_adjusted_down;
  QVariant brush_density_shortcut_adjusted_up;
  QVariant brush_opacity_shortcut_adjusted_down;
  QVariant brush_opacity_shortcut_adjusted_up;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveBrushSize",
          Q_RETURN_ARG(QVariant, brush_size_shortcut_adjusted),
          Q_ARG(QVariant, 0.02)) ||
      !brush_size_shortcut_adjusted.toBool() ||
      editor_preview.activeMaskBrushSize() <= brush_size_before_shortcut ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveBrushHardness",
          Q_RETURN_ARG(QVariant, brush_hardness_shortcut_adjusted),
          Q_ARG(QVariant, 0.08)) ||
      !brush_hardness_shortcut_adjusted.toBool() ||
      editor_preview.activeMaskBrushHardness() <= brush_hardness_before_shortcut ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveBrushFlow",
          Q_RETURN_ARG(QVariant, brush_flow_shortcut_adjusted_down),
          Q_ARG(QVariant, -0.08)) ||
      !brush_flow_shortcut_adjusted_down.toBool() ||
      editor_preview.activeMaskBrushFlow() >= brush_flow_before_shortcut ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveBrushFlow",
          Q_RETURN_ARG(QVariant, brush_flow_shortcut_adjusted_up),
          Q_ARG(QVariant, 0.08)) ||
      !brush_flow_shortcut_adjusted_up.toBool() ||
      std::abs(editor_preview.activeMaskBrushFlow() - brush_flow_before_shortcut) > 1e-6 ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveMaskDensity",
          Q_RETURN_ARG(QVariant, brush_density_shortcut_adjusted_down),
          Q_ARG(QVariant, -0.08)) ||
      !brush_density_shortcut_adjusted_down.toBool() ||
      editor_preview.activeMaskDensity() >= brush_density_before_shortcut ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveMaskDensity",
          Q_RETURN_ARG(QVariant, brush_density_shortcut_adjusted_up),
          Q_ARG(QVariant, 0.08)) ||
      !brush_density_shortcut_adjusted_up.toBool() ||
      std::abs(editor_preview.activeMaskDensity() - brush_density_before_shortcut) > 1e-6 ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveMaskOpacity",
          Q_RETURN_ARG(QVariant, brush_opacity_shortcut_adjusted_down),
          Q_ARG(QVariant, -0.08)) ||
      !brush_opacity_shortcut_adjusted_down.toBool() ||
      editor_preview.activeMaskOpacity() >= brush_opacity_before_shortcut ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveMaskOpacity",
          Q_RETURN_ARG(QVariant, brush_opacity_shortcut_adjusted_up),
          Q_ARG(QVariant, 0.08)) ||
      !brush_opacity_shortcut_adjusted_up.toBool() ||
      std::abs(editor_preview.activeMaskOpacity() - brush_opacity_before_shortcut) > 1e-6) {
    return fail("brush parameter shortcuts should mutate size, hardness, flow, density, and opacity while brush mode is active");
  }
  QVariant brush_add_mode;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setBrushPaintMode",
          Q_RETURN_ARG(QVariant, brush_add_mode),
          Q_ARG(QVariant, QStringLiteral("add"))) ||
      brush_add_mode.toString() != QStringLiteral("add") ||
      editor_preview.activeMaskOperation() != QStringLiteral("add")) {
    return fail("brush HUD should switch the active brush between erase and add modes");
  }
  QCoreApplication::processEvents();
  if (!brush_add_button->property("active").toBool()) {
    return fail("brush HUD add button should reflect the active brush mode");
  }
  QVariant temporary_erase_started;
  QVariant temporary_erase_finished;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setTemporaryBrushEraseFromShortcut",
          Q_RETURN_ARG(QVariant, temporary_erase_started),
          Q_ARG(QVariant, true)) ||
      !temporary_erase_started.toBool() ||
      !edit_workspace->property("temporaryBrushEraseActive").toBool() ||
      editor_preview.activeMaskOperation() != QStringLiteral("subtract") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setTemporaryBrushEraseFromShortcut",
          Q_RETURN_ARG(QVariant, temporary_erase_finished),
          Q_ARG(QVariant, false)) ||
      !temporary_erase_finished.toBool() ||
      edit_workspace->property("temporaryBrushEraseActive").toBool() ||
      editor_preview.activeMaskOperation() != QStringLiteral("add")) {
    return fail("holding the temporary erase shortcut should switch the brush to erase and restore the previous paint mode on release");
  }
  const auto mask_count_before_tool_switch = editor_preview.maskCount();
  editor_preview.beginActiveBrushStroke();
  editor_preview.addBrushPointWithTablet(0.38, 0.44, 0.72, 0.0, 0.0);
  QVariant transient_tool_state_signature;
  if (!editor_preview.brushStrokeActive() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "activateEditTool",
          Q_ARG(QVariant, QVariant(QStringLiteral("adjust")))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "transientToolStateSignature",
          Q_RETURN_ARG(QVariant, transient_tool_state_signature)) ||
      transient_tool_state_signature.toString() !=
          QStringLiteral("tool:adjust|activeMask:false|stroke:false|crop:false|compare:false") ||
      editor_preview.maskCount() != mask_count_before_tool_switch) {
    return fail("switching tools should commit active brush strokes and clear transient mask, crop, and compare state safely");
  }
  QVariant retouch_mode;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "startRetouchTool",
          Q_RETURN_ARG(QVariant, retouch_mode),
          Q_ARG(QVariant, QStringLiteral("heal"))) ||
      retouch_mode.toString() != QStringLiteral("heal")) {
    return fail("edit workspace should expose a direct retouch starter");
  }
  QCoreApplication::processEvents();
  auto* retouch_tool_hud = edit_workspace->findChild<QObject*>(QStringLiteral("retouchToolHud"));
  auto* retouch_heal_button = edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudHealButton"));
  auto* retouch_patch_button = edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudPatchButton"));
  auto* retouch_remove_button = edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudRemoveButton"));
  auto* retouch_remove_add_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudRemoveAddButton"));
  auto* retouch_remove_subtract_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudRemoveSubtractButton"));
  auto* retouch_clone_button = edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudCloneButton"));
  auto* retouch_status_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudStatusLabel"));
  auto* retouch_apply_button = edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudApplyButton"));
  auto* retouch_reset_selection_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudResetSelectionButton"));
  auto* retouch_cancel_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudCancelButton"));
  auto* retouch_reopen_selection_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudReopenSelectionButton"));
  auto* retouch_delete_patch_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudDeletePatchButton"));
  auto* retouch_regenerate_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudRegenerateButton"));
  auto* retouch_finalize_inpaint_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudFinalizeInpaintButton"));
  auto* retouch_retry_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudRetryButton"));
  auto* retouch_before_after_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudBeforeAfterButton"));
  auto* retouch_heal_blend_panel =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudHealBlendPanel"));
  auto* retouch_heal_blend_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudHealBlendLabel"));
  auto* retouch_heal_texture_down_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudHealTextureDownButton"));
  auto* retouch_heal_texture_up_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudHealTextureUpButton"));
  auto* retouch_heal_color_down_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudHealColorDownButton"));
  auto* retouch_heal_color_up_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudHealColorUpButton"));
  auto* retouch_patch_source_panel =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudPatchSourcePanel"));
  auto* retouch_patch_source_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudPatchSourceLabel"));
  auto* retouch_patch_source_left_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudPatchSourceLeftButton"));
  auto* retouch_patch_source_reset_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudPatchSourceResetButton"));
  auto* retouch_clone_aligned_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudCloneAlignedButton"));
  auto* retouch_clone_scale_down_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudCloneScaleDownButton"));
  auto* retouch_clone_scale_up_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudCloneScaleUpButton"));
  auto* retouch_clone_rotate_left_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudCloneRotateLeftButton"));
  auto* retouch_clone_rotate_right_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudCloneRotateRightButton"));
  auto* retouch_clone_source_panel =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudCloneSourcePanel"));
  auto* retouch_clone_save_source_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudCloneSaveSourceButton"));
  auto* retouch_clone_source_count_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudCloneSourceCountLabel"));
  auto* retouch_variation_picker =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudVariationPicker"));
  auto* retouch_skin_button = edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudSkinButton"));
  auto* retouch_smooth_button = edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudSmoothButton"));
  auto* edit_zoom_toolbar_for_retouch = find_named_object(edit_workspace, QStringLiteral("editZoomToolbar"));
  auto* edit_action_toolbar_for_retouch = find_named_object(edit_workspace, QStringLiteral("editActionToolbar"));
  const auto retouch_inpaint_capability =
      editor_preview.aiInpaintAvailable() ? QStringLiteral("model-ready") : QStringLiteral("local-fallback");
  const auto retouch_inpaint_status = editor_preview.inpaintRuntimeLabel();
  if (retouch_tool_hud == nullptr || retouch_heal_button == nullptr || retouch_patch_button == nullptr ||
      retouch_remove_button == nullptr || retouch_remove_add_button == nullptr ||
      retouch_remove_subtract_button == nullptr || retouch_clone_button == nullptr ||
      retouch_status_label == nullptr || retouch_apply_button == nullptr ||
      retouch_reset_selection_button == nullptr || retouch_cancel_button == nullptr ||
      retouch_reopen_selection_button == nullptr || retouch_delete_patch_button == nullptr ||
      retouch_regenerate_button == nullptr || retouch_finalize_inpaint_button == nullptr ||
      retouch_retry_button == nullptr ||
      retouch_before_after_button == nullptr ||
      retouch_heal_blend_panel == nullptr || retouch_heal_blend_label == nullptr ||
      retouch_heal_texture_down_button == nullptr || retouch_heal_texture_up_button == nullptr ||
      retouch_heal_color_down_button == nullptr || retouch_heal_color_up_button == nullptr ||
      retouch_patch_source_panel == nullptr || retouch_patch_source_label == nullptr ||
      retouch_patch_source_left_button == nullptr || retouch_patch_source_reset_button == nullptr ||
      retouch_clone_aligned_button == nullptr || retouch_clone_scale_down_button == nullptr ||
      retouch_clone_scale_up_button == nullptr || retouch_clone_rotate_left_button == nullptr ||
      retouch_clone_rotate_right_button == nullptr ||
      retouch_clone_source_panel == nullptr || retouch_clone_save_source_button == nullptr ||
      retouch_clone_source_count_label == nullptr ||
      retouch_variation_picker == nullptr ||
      retouch_skin_button == nullptr || retouch_smooth_button == nullptr ||
      edit_zoom_toolbar_for_retouch == nullptr || edit_action_toolbar_for_retouch == nullptr ||
      !retouch_tool_hud->property("visible").toBool() ||
      !edit_workspace->property("activeCanvasToolChromeReduced").toBool() ||
      edit_zoom_toolbar_for_retouch->property("visible").toBool() ||
      edit_action_toolbar_for_retouch->property("visible").toBool() ||
      !retouch_heal_button->property("active").toBool() ||
      !retouch_status_label->property("text").toString().contains(QStringLiteral("Heal")) ||
      !retouch_status_label->property("text").toString().contains(QStringLiteral("Paint area")) ||
      !retouch_status_label->property("text").toString().contains(retouch_inpaint_status) ||
      !retouch_apply_button->property("enabled").toBool() ||
      !retouch_reset_selection_button->property("enabled").toBool() ||
      !retouch_cancel_button->property("enabled").toBool() ||
      retouch_reopen_selection_button->property("enabled").toBool() ||
      retouch_delete_patch_button->property("enabled").toBool() ||
      !retouch_regenerate_button->property("enabled").toBool() ||
      retouch_finalize_inpaint_button->property("enabled").toBool() ||
      !retouch_apply_button->property("tooltip").toString().contains(QStringLiteral("edit recipe")) ||
      !retouch_apply_button->property("tooltip").toString().contains(QStringLiteral("original file is unchanged")) ||
      !retouch_regenerate_button->property("tooltip").toString().contains(QStringLiteral("retouch patch candidate")) ||
      !retouch_regenerate_button->property("tooltip").toString().contains(QStringLiteral("original file is unchanged")) ||
      !retouch_finalize_inpaint_button->property("tooltip").toString().contains(QStringLiteral("full-resolution inpaint")) ||
      !retouch_finalize_inpaint_button->property("tooltip").toString().contains(QStringLiteral("preview patches are not final output")) ||
      retouch_before_after_button->property("text").toString() != QStringLiteral("Show Before") ||
      !retouch_before_after_button->property("tooltip").toString().contains(QStringLiteral("pre-retouch image")) ||
      !retouch_before_after_button->property("tooltip").toString().contains(QStringLiteral("edit recipe is unchanged")) ||
      !retouch_heal_blend_panel->property("visible").toBool() ||
      retouch_patch_source_panel->property("visible").toBool() ||
      !retouch_heal_blend_label->property("text").toString().contains(QStringLiteral("Texture")) ||
      !retouch_heal_blend_label->property("text").toString().contains(QStringLiteral("Color")) ||
      !retouch_heal_texture_down_button->property("tooltip").toString().contains(QStringLiteral("texture")) ||
      !retouch_heal_color_up_button->property("tooltip").toString().contains(QStringLiteral("color")) ||
      !retouch_reset_selection_button->property("tooltip").toString().contains(QStringLiteral("Clear the pending retouch selection")) ||
      !retouch_cancel_button->property("tooltip").toString().contains(QStringLiteral("Discard the pending retouch selection")) ||
      !retouch_reopen_selection_button->property("tooltip").toString().contains(QStringLiteral("Reopen the applied retouch selection")) ||
      !retouch_delete_patch_button->property("tooltip").toString().contains(QStringLiteral("Remove the applied retouch patch")) ||
      !retouch_remove_add_button->property("tooltip").toString().contains(QStringLiteral("before applying")) ||
      !retouch_remove_subtract_button->property("tooltip").toString().contains(QStringLiteral("before applying")) ||
      retouch_retry_button->property("visible").toBool() ||
      retouch_variation_picker->property("visible").toBool() ||
      retouch_skin_button->property("visible").toBool() ||
      retouch_smooth_button->property("visible").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchHealBlendSignature",
          Q_RETURN_ARG(QVariant, active_mask_context_signature)) ||
      active_mask_context_signature.toString() !=
          QStringLiteral("visible:true|texture:68|color:46") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchWorkflowSignature",
          Q_RETURN_ARG(QVariant, retouch_mode)) ||
      retouch_mode.toString() !=
          QStringLiteral("mode:heal|label:Heal|stage:Paint area|source:none|apply:true|regenerate:true|capability:%1|beforeAfter:after")
              .arg(retouch_inpaint_capability) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "activeMaskEditingContextSignature",
          Q_RETURN_ARG(QVariant, active_mask_context_signature)) ||
      active_mask_context_signature.toString() !=
          QStringLiteral("tool:retouch|mask:false|brush:false|retouch:true|count:1") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("retouch") ||
      editor_preview.activeMaskToolRole() != QStringLiteral("retouch") ||
      editor_preview.activeMaskRetouchMode() != QStringLiteral("heal")) {
    return fail("retouch workflow should own the canvas HUD without generic edit toolbar overlap");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "startRetouchTool",
          Q_RETURN_ARG(QVariant, retouch_mode),
          Q_ARG(QVariant, QStringLiteral("patch"))) ||
      retouch_mode.toString() != QStringLiteral("patch")) {
    return fail("retouch HUD should start patch mode directly");
  }
  QCoreApplication::processEvents();
  auto* patch_source_region =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportPatchSourceRegion"));
  auto* patch_source_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportPatchSourceLabel"));
  QVariant patch_source_signature;
  if (!retouch_patch_button->property("active").toBool() ||
      patch_source_region == nullptr || patch_source_label == nullptr ||
      !patch_source_region->property("visible").toBool() ||
      !patch_source_label->property("visible").toBool() ||
      !retouch_patch_source_panel->property("visible").toBool() ||
      !retouch_patch_source_label->property("text").toString().contains(QStringLiteral("Source 62,50")) ||
      !retouch_patch_source_left_button->property("tooltip").toString().contains(QStringLiteral("source region")) ||
      !retouch_patch_source_reset_button->property("tooltip").toString().contains(QStringLiteral("Reset")) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchPatchSourceSignature",
          Q_RETURN_ARG(QVariant, patch_source_signature)) ||
      patch_source_signature.toString() !=
          QStringLiteral("visible:true|set:true|x:62|y:50|w:18|h:18") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchWorkflowSignature",
          Q_RETURN_ARG(QVariant, retouch_mode)) ||
      retouch_mode.toString() !=
          QStringLiteral("mode:patch|label:Patch|stage:Paint area|source:region|apply:true|regenerate:true|capability:%1|beforeAfter:after")
              .arg(retouch_inpaint_capability)) {
    return fail("patch retouch should expose its own active mode and workflow state");
  }
  if (!QMetaObject::invokeMethod(
          patch_source_region,
          "moveRegion",
          Q_ARG(QVariant, 0.18),
          Q_ARG(QVariant, 0.22)) ||
      std::abs(editor_preview.activeMaskPatchSourceX() - 0.18) > 1e-6 ||
      std::abs(editor_preview.activeMaskPatchSourceY() - 0.22) > 1e-6 ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchPatchSourceSignature",
          Q_RETURN_ARG(QVariant, patch_source_signature)) ||
      patch_source_signature.toString() !=
          QStringLiteral("visible:true|set:true|x:18|y:22|w:18|h:18")) {
    return fail("patch source overlay should move the source region before applying");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "startRetouchTool",
          Q_RETURN_ARG(QVariant, retouch_mode),
          Q_ARG(QVariant, QStringLiteral("remove"))) ||
      retouch_mode.toString() != QStringLiteral("remove")) {
    return fail("retouch HUD should switch between retouch modes");
  }
  QCoreApplication::processEvents();
  if (!retouch_remove_button->property("active").toBool() ||
      !retouch_status_label->property("text").toString().contains(QStringLiteral("Remove")) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchWorkflowSignature",
          Q_RETURN_ARG(QVariant, retouch_mode)) ||
      retouch_mode.toString() !=
          QStringLiteral("mode:remove|label:Remove|stage:Paint area|source:none|apply:true|regenerate:true|capability:%1|beforeAfter:after")
              .arg(retouch_inpaint_capability) ||
      editor_preview.activeMaskRetouchMode() != QStringLiteral("remove")) {
    return fail("retouch remove button should reflect the active retouch mode");
  }
  editor_preview.startRemoveRefineBrush(true);
  QCoreApplication::processEvents();
  if (!retouch_remove_subtract_button->property("active").toBool() ||
      retouch_remove_add_button->property("active").toBool() ||
      editor_preview.activeMaskOperation() != QStringLiteral("subtract") ||
      editor_preview.activeMaskRetouchMode() != QStringLiteral("remove")) {
    return fail("retouch remove HUD should expose subtract refinement before applying");
  }
  QVariant retouch_recoverability_signature;
  QVariant retouch_primary_tool_signature;
  QVariant retouch_all_workflow_state_signature;
  QVariant retouch_variation_picker_signature;
  QVariant retouch_before_after_signature;
  QVariant retouch_action_signature;
  if (!QMetaObject::invokeMethod(
           edit_workspace,
           "retouchRecoverabilitySignature",
           Q_RETURN_ARG(QVariant, retouch_recoverability_signature)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchPrimaryToolSignature",
          Q_RETURN_ARG(QVariant, retouch_primary_tool_signature)) ||
      !QMetaObject::invokeMethod(
           edit_workspace,
           "retouchAllWorkflowStateSignature",
           Q_RETURN_ARG(QVariant, retouch_all_workflow_state_signature)) ||
      !QMetaObject::invokeMethod(
           edit_workspace,
           "retouchVariationPickerSignature",
           Q_RETURN_ARG(QVariant, retouch_variation_picker_signature)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchBeforeAfterSignature",
          Q_RETURN_ARG(QVariant, retouch_before_after_signature)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchActionSignature",
          Q_RETURN_ARG(QVariant, retouch_action_signature)) ||
      !retouch_recoverability_signature.toString().contains(
          QStringLiteral("mode:remove|state:%1|canvasBlocking:false|apply:true|regenerate:true")
              .arg(retouch_inpaint_capability)) ||
      (editor_preview.aiInpaintAvailable()
           ? !retouch_recoverability_signature.toString().contains(QStringLiteral("modelReason:"))
           : !retouch_recoverability_signature.toString().contains(
                  QStringLiteral("modelReason:LaMa Model Inpaint is not active in this build; retouch will use %1")
                      .arg(editor_preview.inpaintRuntimeLabel()))) ||
      retouch_primary_tool_signature.toString() !=
          QStringLiteral("remove:primary|heal:primary|patch:primary|clone:primary|dust:primary|red-eye:primary|skin-cleanup:experimental-hidden|frequency-smoothing:experimental-hidden") ||
      retouch_all_workflow_state_signature.toString() !=
          QStringLiteral(
              "remove:draft|heal:draft|clone:inactive|patch:draft|dust:inactive|red-eye:inactive") ||
      retouch_variation_picker_signature.toString() !=
          QStringLiteral("visible:false|count:0|index:0") ||
      retouch_before_after_signature.toString() !=
          QStringLiteral("view:after|button:Show Before|recipe:unchanged") ||
      retouch_action_signature.toString() !=
          QStringLiteral("apply:true|cancel:true|reset:true|delete:false|reopen:false")) {
    return fail("retouch inpaint fallback should be recoverable without blocking canvas and experimental tools should stay out of the primary HUD");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "startRetouchTool",
          Q_RETURN_ARG(QVariant, retouch_mode),
          Q_ARG(QVariant, QStringLiteral("clone"))) ||
      retouch_mode.toString() != QStringLiteral("clone")) {
    return fail("retouch HUD should start clone stamp directly");
  }
  QCoreApplication::processEvents();
  if (!retouch_clone_button->property("active").toBool() ||
      !retouch_status_label->property("text").toString().contains(QStringLiteral("Clone Stamp")) ||
      !retouch_status_label->property("text").toString().contains(QStringLiteral("Pick source")) ||
      retouch_regenerate_button->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchWorkflowSignature",
          Q_RETURN_ARG(QVariant, retouch_mode)) ||
      retouch_mode.toString() !=
          QStringLiteral("mode:clone|label:Clone Stamp|stage:Pick source|source:required|apply:true|regenerate:false|capability:deterministic|beforeAfter:after")) {
    return fail("clone stamp HUD should clearly require a source before painting");
  }
  editor_preview.setActiveMaskCloneSource(0.34, 0.46);
  QCoreApplication::processEvents();
  auto* clone_source_pin =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportCloneSourcePin"));
  auto* clone_source_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportCloneSourceLabel"));
  auto* clone_target_preview =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportCloneTargetPreview"));
  auto* clone_offset_line =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportCloneOffsetLine"));
  auto* clone_transform_badge =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportCloneTransformBadge"));
  QVariant retouch_clone_overlay_signature;
  if (clone_source_pin == nullptr || clone_source_label == nullptr ||
      clone_target_preview == nullptr || clone_offset_line == nullptr ||
      clone_transform_badge == nullptr ||
      !clone_source_pin->property("visible").toBool() ||
      !clone_target_preview->property("visible").toBool() ||
      !clone_offset_line->property("visible").toBool() ||
      clone_source_pin->property("sourceStatus").toString() != QStringLiteral("Source set") ||
      clone_target_preview->property("cloneMode").toString() != QStringLiteral("aligned") ||
      clone_transform_badge->property("text").toString().isEmpty() ||
      retouch_clone_aligned_button->property("text").toString() != QStringLiteral("Aligned") ||
      !retouch_clone_aligned_button->property("tooltip").toString().contains(QStringLiteral("continuous")) ||
      !retouch_status_label->property("text").toString().contains(QStringLiteral("Source set")) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchWorkflowSignature",
          Q_RETURN_ARG(QVariant, retouch_mode)) ||
      retouch_mode.toString() !=
          QStringLiteral("mode:clone|label:Clone Stamp|stage:Source set|source:set|apply:true|regenerate:false|capability:deterministic|beforeAfter:after") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchAllWorkflowStateSignature",
          Q_RETURN_ARG(QVariant, retouch_all_workflow_state_signature)) ||
      retouch_all_workflow_state_signature.toString() !=
          QStringLiteral("remove:draft|heal:draft|clone:draft|patch:draft|dust:inactive|red-eye:inactive")) {
    return fail("clone stamp should show source, target, transform, and aligned overlay state after picking a source");
  }
  QVariant retouch_clone_source_panel_signature;
  if (!retouch_clone_source_panel->property("visible").toBool() ||
      retouch_clone_source_panel->property("sourceCount").toInt() != 0 ||
      retouch_clone_source_panel->property("selectedSourceIndex").toInt() != -1 ||
      !retouch_clone_save_source_button->property("enabled").toBool() ||
      retouch_clone_source_count_label->property("text").toString() != QStringLiteral("0 saved") ||
      !editor_preview.saveActiveCloneSource() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchCloneSourcePanelSignature",
          Q_RETURN_ARG(QVariant, retouch_clone_source_panel_signature)) ||
      retouch_clone_source_panel_signature.toString() !=
          QStringLiteral("visible:true|count:1|index:0|first:Source 1")) {
    return fail("clone source panel should save the first Clone Stamp source");
  }
  if (!QMetaObject::invokeMethod(
          clone_source_pin,
          "moveSource",
          Q_ARG(QVariant, 0.62),
          Q_ARG(QVariant, 0.28)) ||
      std::abs(editor_preview.activeMaskCloneSourceX() - 0.62) > 1e-6 ||
      std::abs(editor_preview.activeMaskCloneSourceY() - 0.28) > 1e-6) {
    return fail("clone source pin should move the active clone source");
  }
  editor_preview.setActiveMaskCloneScale(1.3);
  editor_preview.setActiveMaskCloneRotation(-15.0);
  editor_preview.setActiveMaskCloneAligned(false);
  if (!editor_preview.saveActiveCloneSource() ||
      !editor_preview.selectActiveCloneSource(0) ||
      std::abs(editor_preview.activeMaskCloneSourceX() - 0.34) > 1e-6 ||
      !editor_preview.selectActiveCloneSource(1) ||
      std::abs(editor_preview.activeMaskCloneSourceX() - 0.62) > 1e-6 ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchCloneSourcePanelSignature",
          Q_RETURN_ARG(QVariant, retouch_clone_source_panel_signature)) ||
      retouch_clone_source_panel_signature.toString() !=
          QStringLiteral("visible:true|count:2|index:1|first:Source 1") ||
      retouch_clone_source_panel->property("sourceCount").toInt() != 2 ||
      retouch_clone_source_panel->property("selectedSourceIndex").toInt() != 1 ||
      retouch_clone_source_panel->property("firstSourceName").toString() != QStringLiteral("Source 1")) {
    return fail("clone source panel should save and switch between multiple Clone Stamp sources");
  }
  editor_preview.addBrushPoint(0.52, 0.64);
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "retouchCloneOverlaySignature",
          Q_RETURN_ARG(QVariant, retouch_clone_overlay_signature)) ||
      retouch_clone_overlay_signature.toString() !=
          QStringLiteral("visible:true|source:62,28|target:52,64|scale:1.30|rotation:-15|mode:non-aligned") ||
      clone_target_preview->property("cloneMode").toString() != QStringLiteral("non-aligned") ||
      std::abs(clone_target_preview->property("targetX").toDouble() - 0.52) > 1e-6 ||
      std::abs(clone_target_preview->property("targetY").toDouble() - 0.64) > 1e-6 ||
      std::abs(clone_target_preview->property("cloneScale").toDouble() - 1.3) > 1e-6 ||
      std::abs(clone_target_preview->property("cloneRotation").toDouble() + 15.0) > 1e-6 ||
      retouch_clone_aligned_button->property("text").toString() != QStringLiteral("Non-aligned") ||
      clone_offset_line->property("offsetLength").toDouble() <= 1.0) {
    return fail("clone overlay should expose target preview, offset line, source scale, rotation, and non-aligned mode");
  }
  auto* cancel_active_tool_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("cancelActiveToolShortcut"));
  QVariant cancel_result;
  if (cancel_active_tool_shortcut == nullptr ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "cancelActiveTool",
          Q_RETURN_ARG(QVariant, cancel_result)) ||
      !cancel_result.toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
      editor_preview.hasActiveMask() ||
      retouch_tool_hud->property("visible").toBool()) {
    return fail("Escape/cancel should leave retouch mode and return to neutral Adjust state");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activateEditTool",
          Q_ARG(QVariant, QVariant(QStringLiteral("crop")))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "cancelActiveTool",
          Q_RETURN_ARG(QVariant, cancel_result)) ||
      !cancel_result.toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
      editor_preview.cropEditing()) {
    return fail("Escape/cancel should leave Crop without leaving crop editing active");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activateEditTool",
          Q_ARG(QVariant, QVariant(QStringLiteral("compare")))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "cancelActiveTool",
          Q_RETURN_ARG(QVariant, cancel_result)) ||
      !cancel_result.toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
      editor_preview.compareMode()) {
    return fail("Escape/cancel should leave Compare and restore the Adjust workspace");
  }
  edit_workspace->setProperty("diagnosticsPanelOpen", true);
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "cancelActiveTool",
          Q_RETURN_ARG(QVariant, cancel_result)) ||
      !cancel_result.toBool() ||
      edit_workspace->property("diagnosticsPanelOpen").toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("adjust")) {
    return fail("Escape/cancel should close transient diagnostics before changing tools");
  }
  editor_preview.deselectActiveMask();
  edit_workspace->setProperty("editTool", QStringLiteral("adjust"));
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  for (auto iteration = 0; iteration < 6; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* white_balance_panel_reset_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("whiteBalancePanelResetButton"));
  auto* light_panel_reset_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("lightPanelResetButton"));
  auto* color_panel_reset_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("colorPanelResetButton"));
  if (white_balance_panel_reset_button == nullptr || light_panel_reset_button == nullptr ||
      color_panel_reset_button == nullptr || !white_balance_panel_reset_button->property("visible").toBool() ||
      !light_panel_reset_button->property("visible").toBool() ||
      !color_panel_reset_button->property("visible").toBool()) {
    return fail("adjust inspector should expose per-panel reset buttons for white balance, light, and color");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activateEditTool",
          Q_ARG(QVariant, QVariant(QStringLiteral("crop"))))) {
    return fail("edit workspace should expose crop inspector activation for crop detail controls");
  }
  for (auto iteration = 0; iteration < 6; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* crop_rect_reset_button = edit_workspace->findChild<QObject*>(QStringLiteral("resetCropRectButton"));
  auto* transform_reset_button = edit_workspace->findChild<QObject*>(QStringLiteral("resetTransformButton"));
  auto* crop_aspect_combo = edit_workspace->findChild<QObject*>(QStringLiteral("cropAspectPresetComboBox"));
  auto* crop_aspect_input = edit_workspace->findChild<QObject*>(QStringLiteral("cropAspectPresetInput"));
  auto* crop_aspect_add_button = edit_workspace->findChild<QObject*>(QStringLiteral("addCropAspectPresetButton"));
  auto* crop_aspect_remove_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("removeCropAspectPresetButton"));
  if (crop_rect_reset_button == nullptr || transform_reset_button == nullptr || crop_aspect_combo == nullptr ||
      crop_aspect_input == nullptr || crop_aspect_add_button == nullptr || crop_aspect_remove_button == nullptr ||
      !crop_rect_reset_button->property("visible").toBool() ||
      !transform_reset_button->property("visible").toBool() ||
      crop_aspect_combo->property("count").toInt() < 6 ||
      !crop_aspect_combo->property("visible").toBool()) {
    return fail("crop inspector should expose separate crop/transform resets and a discoverable aspect preset menu");
  }
  editor_preview.removeCustomCropAspectPreset(QStringLiteral("13:7"));
  QVariant added_crop_aspect;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "addCropAspectPreset",
          Q_RETURN_ARG(QVariant, added_crop_aspect),
          Q_ARG(QVariant, QStringLiteral("13:7"))) ||
      !added_crop_aspect.toBool()) {
    return fail("studio crop inspector should add custom aspect presets through the shared controller API");
  }
  QCoreApplication::processEvents();
  if (editor_preview.cropPreset() != QStringLiteral("13:7") ||
      !editor_preview.cropAspectPresets().contains(QStringLiteral("13:7")) ||
      !crop_aspect_remove_button->property("enabled").toBool()) {
    return fail("new custom crop aspect presets should become selected and removable in the Studio inspector");
  }
  QVariant removed_crop_aspect;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "removeCropAspectPreset",
          Q_RETURN_ARG(QVariant, removed_crop_aspect),
          Q_ARG(QVariant, QStringLiteral("13:7"))) ||
      !removed_crop_aspect.toBool() || editor_preview.cropPreset() != QStringLiteral("Original") ||
      editor_preview.cropAspectPresets().contains(QStringLiteral("13:7"))) {
    return fail("studio crop inspector should remove custom aspect presets and return to Original when selected");
  }
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));

  if (!editor_preview.addFillLayer(QStringLiteral("#808080"), QStringLiteral("Smoke Fill")) ||
      !editor_preview.addTextLayer(QStringLiteral("Layer"), QStringLiteral("Smoke Text"))) {
    return fail("layer panel smoke setup should create editable layers");
  }
  edit_workspace->setProperty("activePhotoLayerIndex", 0);
  QVariant initial_layer_selected;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, initial_layer_selected),
          Q_ARG(QVariant, QVariant(0)),
          Q_ARG(QVariant, QVariant(false))) ||
      !initial_layer_selected.toBool()) {
    return fail("layer panel smoke setup should select the active layer through the shared selection model");
  }
  for (auto iteration = 0; iteration < 6; ++iteration) {
    QCoreApplication::processEvents();
  }
  if (edit_inspector_panel->property("title").toString() != QStringLiteral("Layers") ||
      !layers_inspector_tab->property("active").toBool() ||
      !photo_layers_group->property("visible").toBool() ||
      white_balance_group->property("visible").toBool()) {
    return fail("layers should live in a dedicated inspector tab instead of the Adjust control stack");
  }
  auto* photo_layer_solo_button = edit_workspace->findChild<QObject*>(QStringLiteral("photoLayerSoloButton"));
  auto* photo_layer_clear_solo_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("photoLayerClearSoloButton"));
  if (photo_layer_solo_button == nullptr || photo_layer_clear_solo_button == nullptr ||
      photo_layer_solo_button->property("text").toString() != QStringLiteral("Isolate") ||
      photo_layer_clear_solo_button->property("visible").toBool()) {
    std::cerr << "Layer isolate smoke state: soloButton="
              << (photo_layer_solo_button == nullptr ? "null" : "present")
              << " text="
              << (photo_layer_solo_button == nullptr
                      ? std::string()
                      : photo_layer_solo_button->property("text").toString().toStdString())
              << " clearButton="
              << (photo_layer_clear_solo_button == nullptr ? "null" : "present")
              << " clearVisible="
              << (photo_layer_clear_solo_button != nullptr &&
                  photo_layer_clear_solo_button->property("visible").toBool())
              << " layerCount=" << editor_preview.photoLayerCount()
              << " soloActive=" << editor_preview.photoLayerSoloActive() << "\n";
    return fail("layer panel should expose an isolate control and hide clear isolation when inactive");
  }
  auto* photo_layer_opacity_label = find_named_object(edit_workspace, QStringLiteral("photoLayerOpacityLabel"));
  auto* photo_layer_opacity_slider =
      find_named_object(edit_workspace, QStringLiteral("photoLayerOpacitySlider"));
  auto* photo_layer_blend_combo =
      find_named_object(edit_workspace, QStringLiteral("photoLayerBlendModeComboBox"));
  auto* photo_layer_clip_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerClippingMaskButton"));
  auto* photo_layer_mask_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskButton"));
  auto* photo_layer_mask_badge = find_named_object(edit_workspace, QStringLiteral("photoLayerMaskBadge"));
  auto* photo_layer_transform_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTransformButton"));
  auto* photo_layer_visibility_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerVisibilityButton"));
  auto* photo_layer_row_solo_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerRowSoloButton"));
  auto* photo_layer_selection_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerSelectionButton"));
  auto* photo_layer_adjustment_command_bar =
      find_named_object(edit_workspace, QStringLiteral("photoLayerAdjustmentCommandBar"));
  auto* selected_layer_header =
      find_named_object(edit_workspace, QStringLiteral("selectedLayerHeader"));
  auto* selected_layer_header_name_label =
      find_named_object(edit_workspace, QStringLiteral("selectedLayerHeaderNameLabel"));
  auto* selected_layer_header_mode_label =
      find_named_object(edit_workspace, QStringLiteral("selectedLayerHeaderModeLabel"));
  auto* selected_layer_header_status_label =
      find_named_object(edit_workspace, QStringLiteral("selectedLayerHeaderStatusLabel"));
  auto* selected_layer_header_changed_groups_label =
      find_named_object(edit_workspace, QStringLiteral("selectedLayerHeaderChangedGroupsLabel"));
  auto* selected_layer_header_thumbnail =
      find_named_object(edit_workspace, QStringLiteral("selectedLayerHeaderThumbnail"));
  auto* selected_layer_header_thumbnail_image =
      find_named_object(edit_workspace, QStringLiteral("selectedLayerHeaderThumbnailImage"));
  auto* copy_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("copyPhotoLayerAdjustmentsButton"));
  auto* paste_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("pastePhotoLayerAdjustmentsButton"));
  auto* paste_global_adjustments_to_photo_layer_button =
      find_named_object(edit_workspace, QStringLiteral("pasteGlobalAdjustmentsToPhotoLayerButton"));
  auto* toggle_photo_layer_adjustments_preview_button =
      find_named_object(edit_workspace, QStringLiteral("togglePhotoLayerAdjustmentsPreviewButton"));
  auto* sync_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("syncPhotoLayerAdjustmentsButton"));
  auto* clear_photo_layer_selection_button =
      find_named_object(edit_workspace, QStringLiteral("clearPhotoLayerSelectionButton"));
  auto* photo_layer_structure_actions =
      find_named_object(edit_workspace, QStringLiteral("photoLayerStructureActions"));
  auto* photo_layer_lock_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerLockButton"));
  auto* photo_layer_move_up_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMoveUpButton"));
  auto* photo_layer_move_down_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMoveDownButton"));
  auto* photo_layer_duplicate_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerDuplicateButton"));
  auto* photo_layer_delete_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerDeleteButton"));
  auto* photo_layer_row = find_named_object(edit_workspace, QStringLiteral("photoLayerRow"));
  auto* photo_layer_thumbnail = find_named_object(edit_workspace, QStringLiteral("photoLayerThumbnail"));
  auto* photo_layer_thumbnail_image =
      find_named_object(edit_workspace, QStringLiteral("photoLayerThumbnailImage"));
  auto* photo_layer_type_icon = find_named_object(edit_workspace, QStringLiteral("photoLayerTypeIcon"));
  auto* photo_layer_name_field = find_named_object(edit_workspace, QStringLiteral("photoLayerNameField"));
  auto* photo_layer_state_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerStateLabel"));
  auto* photo_layer_changed_group_badge_row =
      find_named_object(edit_workspace, QStringLiteral("photoLayerChangedGroupBadgeRow"));
  auto* photo_layer_adjustment_summary_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerAdjustmentSummaryLabel"));
  auto* photo_layer_job_status_chip =
      find_named_object(edit_workspace, QStringLiteral("photoLayerJobStatusChip"));
  auto* base_image_layer_row = find_named_object(edit_workspace, QStringLiteral("baseImageLayerRow"));
  auto* base_image_layer_name_label =
      find_named_object(edit_workspace, QStringLiteral("baseImageLayerNameLabel"));
  auto* base_image_layer_status_label =
      find_named_object(edit_workspace, QStringLiteral("baseImageLayerStatusLabel"));
  auto* base_image_layer_metadata_label =
      find_named_object(edit_workspace, QStringLiteral("baseImageLayerMetadataLabel"));
  auto* base_image_layer_target_area =
      find_named_object(edit_workspace, QStringLiteral("baseImageLayerRowTargetArea"));
  auto* base_image_layer_thumbnail =
      find_named_object(edit_workspace, QStringLiteral("baseImageLayerThumbnail"));
  auto* base_image_layer_thumbnail_image =
      find_named_object(edit_workspace, QStringLiteral("baseImageLayerThumbnailImage"));
  auto* base_image_layer_lock_badge =
      find_named_object(edit_workspace, QStringLiteral("baseImageLayerLockBadge"));
  auto* base_image_global_adjustment_badge =
      find_named_object(edit_workspace, QStringLiteral("baseImageGlobalAdjustmentBadge"));
  auto* base_image_layer_lock_button =
      find_named_object(edit_workspace, QStringLiteral("baseImageLayerLockButton"));
  auto* base_image_layer_visibility_button =
      find_named_object(edit_workspace, QStringLiteral("baseImageLayerVisibilityButton"));
  auto* photo_layer_nested_mask_list =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskList"));
  auto* photo_layers_mask_set_list =
      find_named_object(edit_workspace, QStringLiteral("photoLayersMaskSetList"));
  auto* photo_layer_nested_mask_section_header =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskSectionHeader"));
  auto* photo_layer_nested_mask_section_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskSectionLabel"));
  auto* photo_layer_nested_mask_section_toggle =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskSectionToggle"));
  auto* photo_layer_nested_mask_row =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskRow"));
  auto* photo_layer_nested_mask_name_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskNameLabel"));
  auto* photo_layer_nested_mask_status_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskStatusLabel"));
  auto* photo_layer_nested_mask_job_status_chip =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskJobStatusChip"));
  auto* photo_layer_nested_mask_thumbnail =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskThumbnail"));
  auto* photo_layer_nested_mask_thumbnail_image =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskThumbnailImage"));
  auto* photo_layer_nested_mask_type_icon =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskTypeIcon"));
  auto* photo_layer_nested_mask_order_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskOrderLabel"));
  auto* photo_layer_nested_mask_edit_layer_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskEditLayerButton"));
  auto* photo_layer_nested_mask_select_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskSelectButton"));
  auto* photo_layer_nested_mask_preview_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskPreviewButton"));
  auto* photo_layer_nested_mask_move_up_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskMoveUpButton"));
  auto* photo_layer_nested_mask_move_down_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskMoveDownButton"));
  auto* photo_layer_mask_set_row =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetRow"));
  auto* photo_layer_mask_set_status_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetStatusLabel"));
  auto* photo_layer_mask_set_job_status_chip =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetJobStatusChip"));
  auto* photo_layer_mask_set_thumbnail =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetThumbnail"));
  auto* photo_layer_mask_set_thumbnail_image =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetThumbnailImage"));
  auto* photo_layer_mask_set_adjust_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetAdjustButton"));
  auto* photo_layer_mask_set_select_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetSelectButton"));
  auto* photo_layer_mask_set_preview_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetPreviewButton"));
  auto* photo_layer_text_editor = find_named_object(edit_workspace, QStringLiteral("photoLayerTextEditor"));
  auto* photo_layer_text_content_field =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTextContentField"));
  auto* photo_layer_text_font_field =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTextFontField"));
  auto* photo_layer_text_color_field =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTextColorField"));
  auto* photo_layer_text_size_slider =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTextSizeSlider"));
  auto* photo_layer_text_bold_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTextBoldButton"));
  auto* photo_layer_text_align_center_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTextAlignCenterButton"));
  auto* photo_layer_text_box_width_slider =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTextBoxWidthSlider"));
  auto* photo_layer_text_box_height_slider =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTextBoxHeightSlider"));
  const auto photo_layer_state_text =
      photo_layer_state_label == nullptr ? QString() : photo_layer_state_label->property("text").toString();
  const auto photo_layer_failure_signature =
      photo_layer_row == nullptr ? QString() : photo_layer_row->property("layerFailureSignature").toString();
  const auto photo_layer_failure_active =
      photo_layer_failure_signature.contains(QStringLiteral("failed:true"));
  const auto photo_layer_mask_failure_signature =
      photo_layer_mask_set_row == nullptr
          ? QString()
          : photo_layer_mask_set_row->property("maskFailureSignature").toString();
  const auto nested_mask_failure_signature =
      photo_layer_nested_mask_row == nullptr
          ? QString()
          : photo_layer_nested_mask_row->property("maskFailureSignature").toString();
  QVariant mask_list_partition_signature_variant;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "maskSetListPartitionSignature",
          Q_RETURN_ARG(QVariant, mask_list_partition_signature_variant))) {
    return fail("layer mask list partition should expose a runtime signature");
  }
  const auto mask_list_partition_signature =
      mask_list_partition_signature_variant.toString();
  const auto standalone_mask_list_partition_signature =
      photo_layers_mask_set_list == nullptr
          ? QString()
          : photo_layers_mask_set_list->property("maskListPartitionSignature").toString();
  const auto nested_mask_list_contract =
      photo_layer_nested_mask_list == nullptr
          ? QString()
          : photo_layer_nested_mask_list->property("nestedMaskListContract").toString();
  const auto photo_layer_job_signature =
      photo_layer_job_status_chip == nullptr
          ? QString()
          : photo_layer_job_status_chip->property("jobStatusSignature").toString();
  const auto mask_set_job_signature =
      photo_layer_mask_set_job_status_chip == nullptr
          ? QString()
          : photo_layer_mask_set_job_status_chip->property("jobStatusSignature").toString();
  const auto nested_mask_job_signature =
      photo_layer_nested_mask_job_status_chip == nullptr
          ? QString()
          : photo_layer_nested_mask_job_status_chip->property("jobStatusSignature").toString();
  const auto photo_layer_thumbnail_state_signature =
      photo_layer_thumbnail == nullptr
          ? QString()
          : photo_layer_thumbnail->property("thumbnailStateSignature").toString();
  const auto photo_layer_mask_set_thumbnail_state_signature =
      photo_layer_mask_set_thumbnail == nullptr
          ? QString()
          : photo_layer_mask_set_thumbnail->property("thumbnailStateSignature").toString();
  const auto nested_mask_thumbnail_state_signature =
      photo_layer_nested_mask_thumbnail == nullptr
          ? QString()
          : photo_layer_nested_mask_thumbnail->property("thumbnailStateSignature").toString();
  const auto mask_set_thumbnail_failure_contained =
      photo_layer_mask_set_thumbnail != nullptr &&
      photo_layer_mask_set_thumbnail->property("thumbnailFailureContained").toBool();
  const auto nested_mask_thumbnail_failure_contained =
      photo_layer_nested_mask_thumbnail != nullptr &&
      photo_layer_nested_mask_thumbnail->property("thumbnailFailureContained").toBool();
  const auto photo_layer_thumbnail_backing_width =
      photo_layer_thumbnail == nullptr
          ? 0
          : photo_layer_thumbnail->property("thumbnailBackingWidth").toInt();
  const auto photo_layer_thumbnail_backing_height =
      photo_layer_thumbnail == nullptr
          ? 0
          : photo_layer_thumbnail->property("thumbnailBackingHeight").toInt();
  const auto photo_layer_thumbnail_source_size =
      photo_layer_thumbnail_image == nullptr
          ? QSize()
          : photo_layer_thumbnail_image->property("sourceSize").toSize();
  const auto selected_layer_header_signature =
      selected_layer_header == nullptr
          ? QString()
          : selected_layer_header->property("headerSignature").toString();
  const auto selected_layer_header_thumbnail_source_size =
      selected_layer_header_thumbnail_image == nullptr
          ? QSize()
          : selected_layer_header_thumbnail_image->property("sourceSize").toSize();
  const auto photo_layer_thumbnail_pixel_signature =
      photo_layer_thumbnail == nullptr
          ? QString()
          : photo_layer_thumbnail->property("thumbnailPixelRatioSignature").toString();
  const auto mask_set_thumbnail_pixel_signature =
      photo_layer_mask_set_thumbnail == nullptr
          ? QString()
          : photo_layer_mask_set_thumbnail->property("thumbnailPixelRatioSignature").toString();
  const auto mask_set_thumbnail_source_size =
      photo_layer_mask_set_thumbnail_image == nullptr
          ? QSize()
          : photo_layer_mask_set_thumbnail_image->property("sourceSize").toSize();
  const auto nested_mask_thumbnail_pixel_signature =
      photo_layer_nested_mask_thumbnail == nullptr
          ? QString()
          : photo_layer_nested_mask_thumbnail->property("thumbnailPixelRatioSignature").toString();
  const auto nested_mask_thumbnail_source_size =
      photo_layer_nested_mask_thumbnail_image == nullptr
          ? QSize()
          : photo_layer_nested_mask_thumbnail_image->property("sourceSize").toSize();
  const auto mask_set_status_text =
      photo_layer_mask_set_status_label == nullptr
          ? QString()
          : photo_layer_mask_set_status_label->property("text").toString();
  const auto nested_mask_status_text =
      photo_layer_nested_mask_status_label == nullptr
          ? QString()
          : photo_layer_nested_mask_status_label->property("text").toString();
  const auto nested_mask_section_contract =
      photo_layer_nested_mask_list == nullptr
          ? QString()
          : photo_layer_nested_mask_list->property("maskSectionContract").toString();
  const auto standalone_mask_row_present = photo_layer_mask_set_row != nullptr;
  if (photo_layer_opacity_label == nullptr || photo_layer_opacity_slider == nullptr ||
      photo_layer_blend_combo == nullptr ||
      photo_layer_clip_button == nullptr || photo_layer_mask_button == nullptr ||
      photo_layer_mask_badge == nullptr || photo_layer_transform_button == nullptr ||
      photo_layer_visibility_button == nullptr || photo_layer_row_solo_button == nullptr ||
      photo_layer_selection_button == nullptr || photo_layer_adjustment_command_bar == nullptr ||
      selected_layer_header == nullptr || selected_layer_header_name_label == nullptr ||
      selected_layer_header_mode_label == nullptr || selected_layer_header_status_label == nullptr ||
      selected_layer_header_changed_groups_label == nullptr ||
      selected_layer_header_thumbnail == nullptr ||
      selected_layer_header_thumbnail_image == nullptr ||
      copy_photo_layer_adjustments_button == nullptr ||
      paste_photo_layer_adjustments_button == nullptr ||
      paste_global_adjustments_to_photo_layer_button == nullptr ||
      toggle_photo_layer_adjustments_preview_button == nullptr ||
      sync_photo_layer_adjustments_button == nullptr ||
      clear_photo_layer_selection_button == nullptr ||
      photo_layer_structure_actions == nullptr || photo_layer_lock_button == nullptr ||
      photo_layer_move_up_button == nullptr || photo_layer_move_down_button == nullptr ||
      photo_layer_duplicate_button == nullptr || photo_layer_delete_button == nullptr ||
      photo_layer_row == nullptr || photo_layer_thumbnail == nullptr ||
      photo_layer_thumbnail_image == nullptr || photo_layer_type_icon == nullptr ||
      photo_layer_name_field == nullptr || photo_layer_state_label == nullptr ||
      photo_layer_job_status_chip == nullptr ||
      base_image_layer_row == nullptr || base_image_layer_name_label == nullptr ||
      base_image_layer_status_label == nullptr || base_image_layer_metadata_label == nullptr ||
      base_image_layer_target_area == nullptr ||
      base_image_layer_thumbnail == nullptr || base_image_layer_thumbnail_image == nullptr ||
      base_image_layer_lock_badge == nullptr || base_image_global_adjustment_badge == nullptr ||
      base_image_layer_lock_button == nullptr ||
      base_image_layer_visibility_button == nullptr ||
      !item_visible(qobject_cast<QQuickItem*>(base_image_layer_row)) ||
      !base_image_layer_row->property("baseLayerLocked").toBool() ||
      base_image_layer_row->property("baseLayerStackPosition").toString() !=
          QStringLiteral("bottom") ||
      base_image_layer_row->property("baseLayerTargetKind").toString() !=
          QStringLiteral("global-edits") ||
      !base_image_layer_row->property("baseLayerContract").toString().contains(
          QStringLiteral("base-image-layer:v1|locked:true|stack:bottom|target:global-edits")) ||
      base_image_layer_name_label->property("text").toString() != QStringLiteral("Base Image") ||
      !base_image_layer_status_label->property("text").toString().contains(
          QStringLiteral("Global edits target")) ||
      !base_image_layer_status_label->property("text").toString().contains(
          QStringLiteral("Locked bottom layer")) ||
      base_image_layer_metadata_label->property("text").toString().trimmed().isEmpty() ||
      base_image_layer_row->property("changedAdjustmentGroupSignature").toString().isEmpty() ||
      base_image_layer_thumbnail->property("thumbnailSource").toString() !=
          editor_preview.baseImageThumbnailSource() ||
      base_image_layer_thumbnail_image->property("source").toString() !=
          editor_preview.baseImageThumbnailSource() ||
      !base_image_layer_lock_button->property("active").toBool() ||
      base_image_layer_lock_button->property("enabled").toBool() ||
      base_image_layer_visibility_button->property("enabled").toBool() ||
      photo_layer_nested_mask_list == nullptr ||
      photo_layer_nested_mask_section_header == nullptr ||
      photo_layer_nested_mask_section_label == nullptr ||
      photo_layer_nested_mask_section_toggle == nullptr ||
      !nested_mask_section_contract.contains(QStringLiteral("mask-section:v1|scope:layer|key:")) ||
      !nested_mask_section_contract.contains(QStringLiteral("|count:")) ||
      !nested_mask_section_contract.contains(QStringLiteral("|collapsed:false")) ||
      photo_layer_nested_mask_list->property("maskSectionCollapsed").toBool() ||
      photo_layer_nested_mask_section_label->property("text").toString().trimmed().isEmpty() ||
      !photo_layer_nested_mask_section_toggle->property("active").toBool() ||
      photo_layer_nested_mask_row == nullptr ||
      photo_layer_nested_mask_name_label == nullptr ||
      photo_layer_nested_mask_status_label == nullptr ||
      photo_layer_nested_mask_job_status_chip == nullptr ||
      photo_layer_nested_mask_thumbnail == nullptr ||
      photo_layer_nested_mask_thumbnail_image == nullptr ||
      photo_layer_nested_mask_type_icon == nullptr ||
      photo_layer_nested_mask_order_label == nullptr ||
      photo_layer_nested_mask_edit_layer_button == nullptr ||
      photo_layer_nested_mask_select_button == nullptr ||
      photo_layer_nested_mask_preview_button == nullptr ||
      photo_layer_nested_mask_move_up_button == nullptr ||
      photo_layer_nested_mask_move_down_button == nullptr ||
      !item_visible(qobject_cast<QQuickItem*>(photo_layer_nested_mask_row)) ||
      !photo_layer_nested_mask_row->property("maskChildAttachment").toBool() ||
      !mask_list_partition_signature.contains(QStringLiteral("mask-list-partition:v1")) ||
      !mask_list_partition_signature.contains(QStringLiteral("|duplicates:0")) ||
      standalone_mask_list_partition_signature != mask_list_partition_signature ||
      !nested_mask_list_contract.contains(QStringLiteral("nested-mask-list:linked-only|layer:")) ||
      !nested_mask_list_contract.contains(QStringLiteral("|count:")) ||
      photo_layer_nested_mask_row->property("parentLayerIndex").toInt() < 0 ||
      photo_layer_nested_mask_row->property("maskChildIndent").toInt() < 18 ||
      !photo_layer_nested_mask_row->property("maskChildRowContract").toString().contains(
          QStringLiteral("nested-mask-child:v1|indent:18|kind:")) ||
      !photo_layer_nested_mask_row->property("maskChildRowContract").toString().contains(
          QStringLiteral("|combine:")) ||
      !photo_layer_nested_mask_row->property("maskChildRowContract").toString().contains(
          QStringLiteral("|order:")) ||
      photo_layer_nested_mask_row->property("combineOrderIndex").toInt() < 0 ||
      photo_layer_nested_mask_row->property("combineOrderCount").toInt() < 1 ||
      photo_layer_nested_mask_row->property("combineOrderLabel").toString().trimmed().isEmpty() ||
      photo_layer_nested_mask_order_label->property("combineOrderLabel").toString() !=
          photo_layer_nested_mask_row->property("combineOrderLabel").toString() ||
      photo_layer_nested_mask_row->property("combineModeSummary").toString().trimmed().isEmpty() ||
      photo_layer_nested_mask_row->property("combineModeContract").toString().trimmed().isEmpty() ||
      !photo_layer_nested_mask_row->property("combineModeContract").toString().contains(
          QStringLiteral("mask-combine:v1")) ||
      photo_layer_nested_mask_row->property("primarySourceKind").toString().trimmed().isEmpty() ||
      photo_layer_nested_mask_type_icon->property("sourceKind").toString() !=
          photo_layer_nested_mask_row->property("primarySourceKind").toString() ||
      !photo_layer_nested_mask_type_icon->property("iconContract").toString().contains(
          QStringLiteral("mask-type-icon:v1|kind:")) ||
      !item_visible(qobject_cast<QQuickItem*>(selected_layer_header)) ||
      !selected_layer_header_signature.contains(QStringLiteral("selected-layer-header:v1")) ||
      !selected_layer_header_signature.contains(QStringLiteral("kind:")) ||
      !selected_layer_header_signature.contains(QStringLiteral("visible:Visible")) ||
      !selected_layer_header_signature.contains(QStringLiteral("opacity:")) ||
      !selected_layer_header_signature.contains(QStringLiteral("blend:")) ||
      !selected_layer_header_signature.contains(QStringLiteral("mask:")) ||
      selected_layer_header_mode_label->property("text").toString() !=
          QStringLiteral("Editing Layer") ||
      selected_layer_header_name_label->property("text").toString().trimmed().isEmpty() ||
      !selected_layer_header_status_label->property("text").toString().contains(
          QStringLiteral(" / ")) ||
      selected_layer_header_changed_groups_label->property("text").toString().trimmed().isEmpty() ||
      selected_layer_header_thumbnail_source_size.width() <
          selected_layer_header->property("thumbnailBackingWidth").toInt() ||
      selected_layer_header_thumbnail_source_size.height() <
          selected_layer_header->property("thumbnailBackingHeight").toInt() ||
      photo_layer_changed_group_badge_row == nullptr ||
      photo_layer_adjustment_summary_label == nullptr ||
      (standalone_mask_row_present &&
       (photo_layer_mask_set_adjust_button == nullptr ||
         photo_layer_mask_set_preview_button == nullptr ||
         photo_layer_mask_set_status_label == nullptr ||
        photo_layer_mask_set_job_status_chip == nullptr ||
        photo_layer_mask_set_thumbnail_image == nullptr ||
        photo_layer_mask_set_select_button == nullptr ||
        photo_layer_mask_set_row->property("clickRouteContract").toString() !=
            QStringLiteral("mask-row-click:select-mask|adjust-button:edit-layer"))) ||
      photo_layer_nested_mask_name_label->property("text").toString().trimmed().isEmpty() ||
      (!nested_mask_thumbnail_failure_contained &&
       !nested_mask_status_text.contains(QStringLiteral("mask attachment"))) ||
      photo_layer_text_editor == nullptr || photo_layer_text_content_field == nullptr ||
      photo_layer_text_font_field == nullptr || photo_layer_text_color_field == nullptr ||
      photo_layer_text_size_slider == nullptr || photo_layer_text_bold_button == nullptr ||
      photo_layer_text_align_center_button == nullptr || photo_layer_text_box_width_slider == nullptr ||
      photo_layer_text_box_height_slider == nullptr ||
      (!photo_layer_failure_active && !photo_layer_state_text.contains(QStringLiteral("Visible"))) ||
      (!photo_layer_failure_active && !photo_layer_state_text.contains(QStringLiteral("Selected"))) ||
      (photo_layer_failure_active &&
       !photo_layer_state_text.contains(QStringLiteral("Preview failed:"))) ||
      !photo_layer_row->property("layerStateSignature").toString().contains(
          QStringLiteral("visible:true")) ||
      !photo_layer_row->property("layerStateSignature").toString().contains(
          QStringLiteral("hidden:false")) ||
      !photo_layer_row->property("layerStateSignature").toString().contains(
          QStringLiteral("selected:true")) ||
      !photo_layer_failure_signature.contains(QStringLiteral("row:layer|")) ||
      (photo_layer_failure_active &&
       photo_layer_failure_signature.contains(QStringLiteral("stage:|"))) ||
      (standalone_mask_row_present &&
       !photo_layer_mask_failure_signature.contains(QStringLiteral("failed:false"))) ||
      !nested_mask_failure_signature.contains(QStringLiteral("failed:false")) ||
      !photo_layer_job_signature.contains(QStringLiteral("row:photo-layer")) ||
      !photo_layer_job_signature.contains(QStringLiteral("visible:false")) ||
      photo_layer_job_status_chip->property("visible").toBool() ||
      (standalone_mask_row_present &&
       (!mask_set_job_signature.contains(QStringLiteral("row:mask-set")) ||
        !mask_set_job_signature.contains(QStringLiteral("visible:false")) ||
        photo_layer_mask_set_job_status_chip->property("visible").toBool())) ||
      !nested_mask_job_signature.contains(QStringLiteral("row:nested-mask")) ||
      !nested_mask_job_signature.contains(QStringLiteral("visible:false")) ||
      photo_layer_nested_mask_job_status_chip->property("visible").toBool() ||
      !photo_layer_thumbnail->property("thumbnailReady").toBool() ||
      !photo_layer_thumbnail->property("thumbnailSource").toString().startsWith(
          QStringLiteral("data:image/png;base64,")) ||
      !photo_layer_thumbnail->property("thumbnailDisplaySource").toString().startsWith(
          QStringLiteral("data:image/png;base64,")) ||
      (!photo_layer_thumbnail->property("thumbnailEffectPreviewSource").toString().isEmpty() &&
       (photo_layer_thumbnail->property("thumbnailEffectPreviewContract").toString() !=
            QStringLiteral("adjustment-layer-before-after-thumbnail-v1") ||
        !photo_layer_thumbnail->property("thumbnailUsesEffectPreview").toBool())) ||
      !photo_layer_thumbnail->property("thumbnailSignature").toString().contains(
          QStringLiteral("ready:true")) ||
      photo_layer_thumbnail->property("thumbnailStale").toBool() ||
      photo_layer_thumbnail->property("thumbnailFailureContained").toBool() ||
      !photo_layer_thumbnail_state_signature.contains(QStringLiteral("stale:false")) ||
      !photo_layer_thumbnail_state_signature.contains(QStringLiteral("failure-contained:false")) ||
      photo_layer_thumbnail_backing_width < 64 ||
      photo_layer_thumbnail_backing_height < 64 ||
      photo_layer_thumbnail->property("thumbnailDevicePixelRatio").toDouble() < 2.0 ||
      !photo_layer_thumbnail_pixel_signature.contains(QStringLiteral("display:32x32")) ||
      photo_layer_thumbnail_source_size.width() < photo_layer_thumbnail_backing_width ||
      photo_layer_thumbnail_source_size.height() < photo_layer_thumbnail_backing_height ||
      (standalone_mask_row_present &&
       (photo_layer_mask_set_thumbnail == nullptr ||
        !photo_layer_mask_set_thumbnail_state_signature.contains(QStringLiteral("stale:")) ||
        !photo_layer_mask_set_thumbnail_state_signature.contains(
            QStringLiteral("failure-contained:")))) ||
      (standalone_mask_row_present && mask_set_thumbnail_failure_contained &&
       (!photo_layer_mask_set_thumbnail_state_signature.contains(QStringLiteral("stage:thumbnail_")) ||
        !mask_set_status_text.contains(QStringLiteral("Thumbnail ")))) ||
      (standalone_mask_row_present && !mask_set_thumbnail_failure_contained &&
       (mask_set_status_text.contains(QStringLiteral("Thumbnail ")) ||
         photo_layer_mask_set_thumbnail->property("thumbnailBackingWidth").toInt() < 96 ||
         photo_layer_mask_set_thumbnail->property("thumbnailBackingHeight").toInt() < 96 ||
         photo_layer_mask_set_thumbnail->property("thumbnailDevicePixelRatio").toDouble() < 2.0 ||
          mask_set_thumbnail_source_size.width() <
              photo_layer_mask_set_thumbnail->property("thumbnailBackingWidth").toInt() ||
          mask_set_thumbnail_source_size.height() <
              photo_layer_mask_set_thumbnail->property("thumbnailBackingHeight").toInt() ||
          !mask_set_thumbnail_pixel_signature.contains(QStringLiteral("display:34x34")))) ||
      !nested_mask_thumbnail_state_signature.contains(QStringLiteral("stale:")) ||
      !nested_mask_thumbnail_state_signature.contains(QStringLiteral("failure-contained:")) ||
      (nested_mask_thumbnail_failure_contained &&
       (!nested_mask_thumbnail_state_signature.contains(QStringLiteral("stage:thumbnail_")) ||
        !nested_mask_status_text.contains(QStringLiteral("Thumbnail ")))) ||
      (!nested_mask_thumbnail_failure_contained &&
       (nested_mask_status_text.contains(QStringLiteral("Thumbnail ")) ||
          photo_layer_nested_mask_thumbnail->property("thumbnailBackingWidth").toInt() < 96 ||
          photo_layer_nested_mask_thumbnail->property("thumbnailBackingHeight").toInt() < 96 ||
          photo_layer_nested_mask_thumbnail->property("thumbnailDevicePixelRatio").toDouble() < 2.0 ||
          nested_mask_thumbnail_source_size.width() <
              photo_layer_nested_mask_thumbnail->property("thumbnailBackingWidth").toInt() ||
          nested_mask_thumbnail_source_size.height() <
              photo_layer_nested_mask_thumbnail->property("thumbnailBackingHeight").toInt() ||
          !nested_mask_thumbnail_pixel_signature.contains(QStringLiteral("display:34x34")))) ||
      !photo_layer_thumbnail_image->property("visible").toBool() ||
      !photo_layer_row->property("layoutFits").toBool() ||
      photo_layer_row->property("height").toDouble() > 330.0 ||
      find_named_object(edit_workspace, QStringLiteral("photoLayerOpacityDownButton")) != nullptr ||
      find_named_object(edit_workspace, QStringLiteral("photoLayerOpacityUpButton")) != nullptr) {
    std::cerr << "Layer row control presence: opacityLabel="
              << (photo_layer_opacity_label == nullptr ? "null" : "present")
              << " opacitySlider=" << (photo_layer_opacity_slider == nullptr ? "null" : "present")
              << " blendCombo=" << (photo_layer_blend_combo == nullptr ? "null" : "present")
              << " clip=" << (photo_layer_clip_button == nullptr ? "null" : "present")
              << " mask=" << (photo_layer_mask_button == nullptr ? "null" : "present")
              << " maskBadge=" << (photo_layer_mask_badge == nullptr ? "null" : "present")
              << " transform=" << (photo_layer_transform_button == nullptr ? "null" : "present")
              << " visibility=" << (photo_layer_visibility_button == nullptr ? "null" : "present")
              << " rowSolo=" << (photo_layer_row_solo_button == nullptr ? "null" : "present")
              << " selection="
              << (photo_layer_selection_button == nullptr ? "null" : "present")
               << " commandBar="
               << (photo_layer_adjustment_command_bar == nullptr ? "null" : "present")
              << " selectedLayerHeader="
              << (selected_layer_header == nullptr
                      ? std::string("null")
                      : selected_layer_header_signature.toStdString())
              << " structure=" << (photo_layer_structure_actions == nullptr ? "null" : "present")
              << " nameField=" << (photo_layer_name_field == nullptr ? "null" : "present")
              << " thumbnail="
              << (photo_layer_thumbnail == nullptr
                      ? std::string("null")
                      : photo_layer_thumbnail->property("thumbnailSource")
                            .toString()
                            .left(24)
                            .toStdString())
              << " rowSignature="
              << (photo_layer_row == nullptr
                      ? std::string("null")
                      : photo_layer_row->property("layoutSignature").toString().toStdString())
              << " layerFailureSignature="
              << (photo_layer_row == nullptr
                      ? std::string("null")
                      : photo_layer_failure_signature.toStdString())
              << " maskFailureSignature="
              << (photo_layer_mask_set_row == nullptr
                      ? std::string("null")
                      : photo_layer_mask_failure_signature.toStdString())
              << " nestedMaskFailureSignature="
              << (photo_layer_nested_mask_row == nullptr
                      ? std::string("null")
                      : nested_mask_failure_signature.toStdString())
              << " layerThumbnailState="
              << photo_layer_thumbnail_state_signature.toStdString()
              << " maskSetThumbnailState="
              << photo_layer_mask_set_thumbnail_state_signature.toStdString()
              << " nestedMaskThumbnailState="
              << nested_mask_thumbnail_state_signature.toStdString()
              << " layerCount=" << editor_preview.photoLayerCount() << "\n";
    return fail("layer panel rows should fit their controls without overlap and use slider/dropdown layer controls");
  }
  QVariant base_row_layer_selected;
  QVariant base_row_scope;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, base_row_layer_selected),
          Q_ARG(QVariant, QVariant(0)),
          Q_ARG(QVariant, QVariant(false))) ||
      !base_row_layer_selected.toBool()) {
    return fail("Base Image row click proof should start from an editable photo-layer target");
  }
  process_layout_events(4);
  QVariant base_row_clicked;
  if (!QMetaObject::invokeMethod(
          base_image_layer_target_area,
          "triggerBaseImageLayerRowClickForTest",
          Q_RETURN_ARG(QVariant, base_row_clicked)) ||
      !base_row_clicked.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "activeEditTargetScope",
          Q_RETURN_ARG(QVariant, base_row_scope)) ||
      base_row_scope.toString() != QStringLiteral("base-image") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
      edit_workspace->property("editInspectorTab").toString() != QStringLiteral("adjust")) {
    std::cerr << "Base Image row click state: scope="
              << base_row_scope.toString().toStdString()
              << " tool=" << edit_workspace->property("editTool").toString().toStdString()
              << " tab=" << edit_workspace->property("editInspectorTab").toString().toStdString()
              << " routeClicked=" << base_row_clicked.toBool()
              << " targetAreaVisible="
              << item_visible(qobject_cast<QQuickItem*>(base_image_layer_target_area)) << '\n';
    return fail("clicking the Base Image row should switch to the Base Image edit target");
  }
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, base_row_layer_selected),
          Q_ARG(QVariant, QVariant(0)),
          Q_ARG(QVariant, QVariant(false))) ||
      !base_row_layer_selected.toBool()) {
    return fail("Base Image row click proof should restore the layer panel target after assertion");
  }
  process_layout_events(4);
  const auto base_image_layer_rect = scene_rect(qobject_cast<QQuickItem*>(base_image_layer_row));
  const auto photo_layer_rows = find_named_items(edit_workspace, QStringLiteral("photoLayerRow"));
  if (photo_layer_rows.size() < editor_preview.photoLayerCount()) {
    return fail("layer panel should expose one visible row for every editable photo layer");
  }
  const auto nested_parent_layer_index =
      photo_layer_nested_mask_row == nullptr
          ? -1
          : photo_layer_nested_mask_row->property("parentLayerIndex").toInt();
  const auto failure_layer_index =
      nested_parent_layer_index >= 0 ? nested_parent_layer_index
                                     : photo_layer_row->property("layerIndex").toInt();
  const auto failure_layer_model = editor_preview.photoLayers().value(failure_layer_index).toMap();
  const auto failure_layer_id = failure_layer_model.value(QStringLiteral("id")).toString();
  auto failure_mask_set_ids = failure_layer_model.value(QStringLiteral("selectionMaskSetIds")).toStringList();
  if (failure_mask_set_ids.isEmpty()) {
    const auto raw_mask_set_ids =
        failure_layer_model.value(QStringLiteral("selectionMaskSetIds")).toList();
    for (const auto& raw_mask_set_id : raw_mask_set_ids) {
      const auto mask_set_id = raw_mask_set_id.toString().trimmed();
      if (!mask_set_id.isEmpty()) {
        failure_mask_set_ids.append(mask_set_id);
      }
    }
  }
  if (failure_mask_set_ids.isEmpty()) {
    const auto mask_set_id =
        failure_layer_model.value(QStringLiteral("selectionMaskSetId")).toString().trimmed();
    if (!mask_set_id.isEmpty()) {
      failure_mask_set_ids.append(mask_set_id);
    }
  }
  if (failure_mask_set_ids.isEmpty()) {
    for (const auto& mask_set_value : editor_preview.maskSets()) {
      const auto mask_set = mask_set_value.toMap();
      const auto mask_set_id = mask_set.value(QStringLiteral("id")).toString().trimmed();
      if (!mask_set_id.isEmpty()) {
        failure_mask_set_ids.append(mask_set_id);
        break;
      }
    }
  }
  const auto failure_mask_set_id =
      failure_mask_set_ids.isEmpty() ? QString() : failure_mask_set_ids.constFirst();
  const auto current_file_path =
      photo_editor::desktop::EditorPreviewControllerTestAccess::file_path(editor_preview);
  if (failure_layer_id.trimmed().isEmpty() || failure_mask_set_id.trimmed().isEmpty() ||
      current_file_path.trimmed().isEmpty()) {
    std::cerr << "Failure row precondition: layerIndex=" << failure_layer_index
              << " layerId=" << failure_layer_id.toStdString()
              << " maskSetId=" << failure_mask_set_id.toStdString()
              << " filePathEmpty=" << current_file_path.trimmed().isEmpty()
              << " photoLayerCount=" << editor_preview.photoLayerCount()
              << " maskSetCount=" << editor_preview.maskSetCount() << "\n";
    return fail("preview failure row proof should have an addressable layer, mask set, file, and last preview");
  }
  const auto seed_display_preview_source =
      editor_preview.displayPreviewSource().trimmed().isEmpty()
          ? QStringLiteral("image://desktop-smoke/last-good-preview")
          : editor_preview.displayPreviewSource();
  photo_editor::desktop::RenderResult last_good_result;
  last_good_result.file_path = current_file_path;
  last_good_result.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(editor_preview);
  last_good_result.success = true;
  last_good_result.status = QStringLiteral("Preview ready.");
  last_good_result.render_reason = QStringLiteral("desktop_smoke_last_good");
  last_good_result.render_mode = QStringLiteral("live_preview");
  last_good_result.interaction_mode = QStringLiteral("draft_interactive");
  last_good_result.dirty_domain = QStringLiteral("layers_composite");
  last_good_result.dirty_stages = QStringList{QStringLiteral("layers_composite")};
  last_good_result.preview_image = QImage(24, 16, QImage::Format_RGB32);
  last_good_result.preview_image.fill(QColor(18, 34, 42));
  last_good_result.output_width = last_good_result.preview_image.width();
  last_good_result.output_height = last_good_result.preview_image.height();
  last_good_result.source_width = last_good_result.preview_image.width();
  last_good_result.source_height = last_good_result.preview_image.height();
  last_good_result.display_preview_source = seed_display_preview_source;
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      editor_preview,
      false,
      last_good_result);
  process_layout_events(4);
  const auto last_good_preview_source = editor_preview.displayPreviewSource();
  photo_editor::desktop::RenderResult failed_mask_result;
  failed_mask_result.file_path = current_file_path;
  failed_mask_result.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(editor_preview);
  failed_mask_result.success = false;
  failed_mask_result.failure_stage = QStringLiteral("mask_alpha");
  failed_mask_result.failure_detail = QStringLiteral("sha1:desktop-smoke-mask-alpha");
  failed_mask_result.failure_layer_ids = QStringList{failure_layer_id};
  failed_mask_result.failure_mask_set_ids = QStringList{failure_mask_set_id};
  failed_mask_result.render_reason = QStringLiteral("mask_preview_change");
  failed_mask_result.render_mode = QStringLiteral("settled_preview");
  failed_mask_result.interaction_mode = QStringLiteral("interactive");
  failed_mask_result.dirty_domain = QStringLiteral("mask_rasterization");
  failed_mask_result.dirty_stages =
      QStringList{QStringLiteral("mask_rasterization"), QStringLiteral("mask_composition")};
  failed_mask_result.active_target = QStringLiteral("mask");
  failed_mask_result.active_layer_id = failure_layer_id;
  failed_mask_result.active_mask_set_id = failure_mask_set_id;
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      editor_preview,
      false,
      failed_mask_result);
  process_layout_events(8);
  photo_layer_nested_mask_row =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskRow"));
  photo_layer_nested_mask_status_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskStatusLabel"));
  auto* failure_preview_retry_button =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportPreviewRetryButton"));
  auto* failure_preview_update_badge =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportPreviewUpdateBadge"));
  QString failed_layer_signature;
  QString failed_layer_state_text;
  for (const auto* layer_row_item :
       find_named_items(edit_workspace, QStringLiteral("photoLayerRow"))) {
    const auto row_signature = layer_row_item->property("layerFailureSignature").toString();
    if (row_signature.contains(QStringLiteral("failed:true")) &&
        row_signature.contains(QStringLiteral("stage:mask_alpha")) &&
        row_signature.contains(failure_layer_id) &&
        row_signature.contains(failure_mask_set_id)) {
      failed_layer_signature = row_signature;
      const auto row_labels =
          layer_row_item->findChildren<QObject*>(QStringLiteral("photoLayerStateLabel"));
      if (!row_labels.isEmpty()) {
        failed_layer_state_text = row_labels.constFirst()->property("text").toString();
      }
      break;
    }
  }
  const auto failed_mask_signature =
      photo_layer_nested_mask_row == nullptr
          ? QString()
          : photo_layer_nested_mask_row->property("maskFailureSignature").toString();
  const auto failed_mask_status =
      photo_layer_nested_mask_status_label == nullptr
          ? QString()
          : photo_layer_nested_mask_status_label->property("text").toString();
  if (photo_layer_nested_mask_row == nullptr || photo_layer_nested_mask_status_label == nullptr ||
      failure_preview_retry_button == nullptr || failure_preview_update_badge == nullptr ||
      editor_preview.status() != QStringLiteral("Preview failed: Mask alpha.") ||
      editor_preview.previewFailureStage() != QStringLiteral("mask_alpha") ||
      !editor_preview.previewFailureLayerIds().contains(failure_layer_id) ||
      !editor_preview.previewFailureMaskSetIds().contains(failure_mask_set_id) ||
      editor_preview.displayPreviewSource() != last_good_preview_source ||
      !failure_preview_retry_button->property("visible").toBool() ||
      !failure_preview_update_badge->property("visible").toBool() ||
      failed_layer_signature.trimmed().isEmpty() ||
      failed_layer_state_text.trimmed().isEmpty() ||
      !failed_layer_state_text.contains(QStringLiteral("Preview failed: Mask alpha")) ||
      !failed_mask_status.contains(QStringLiteral("Preview failed: Mask alpha")) ||
      !failed_mask_signature.contains(QStringLiteral("failed:true")) ||
      !failed_mask_signature.contains(QStringLiteral("stage:mask_alpha")) ||
      !failed_mask_signature.contains(failure_mask_set_id)) {
    std::cerr << "Failure row injection: status=" << editor_preview.status().toStdString()
              << " stage=" << editor_preview.previewFailureStage().toStdString()
              << " layerIds=" << editor_preview.previewFailureLayerIds().join(',').toStdString()
              << " maskSetIds=" << editor_preview.previewFailureMaskSetIds().join(',').toStdString()
              << " layerSignature=" << failed_layer_signature.toStdString()
              << " layerState=" << failed_layer_state_text.toStdString()
              << " maskSignature=" << failed_mask_signature.toStdString()
              << " maskStatus=" << failed_mask_status.toStdString()
              << " retryVisible="
              << (failure_preview_retry_button == nullptr
                      ? false
                      : failure_preview_retry_button->property("visible").toBool())
              << "\n";
    return fail("preview failure should stay on the last good canvas and mark the affected layer and mask rows");
  }
  last_good_result.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(editor_preview);
  last_good_result.render_reason = QStringLiteral("desktop_smoke_failure_recovery");
  last_good_result.display_preview_source = last_good_preview_source;
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      editor_preview,
      false,
      last_good_result);
  process_layout_events(8);
  photo_layer_nested_mask_row =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskRow"));
  failure_preview_retry_button =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportPreviewRetryButton"));
  auto recovered_layer_failed = false;
  for (const auto* layer_row_item :
       find_named_items(edit_workspace, QStringLiteral("photoLayerRow"))) {
    if (layer_row_item->property("layerFailureSignature")
            .toString()
            .contains(QStringLiteral("failed:true"))) {
      recovered_layer_failed = true;
      break;
    }
  }
  const auto recovered_mask_signature =
      photo_layer_nested_mask_row == nullptr
          ? QString()
          : photo_layer_nested_mask_row->property("maskFailureSignature").toString();
  if (!editor_preview.previewFailureStage().isEmpty() ||
      !editor_preview.previewFailureLayerIds().isEmpty() ||
      !editor_preview.previewFailureMaskSetIds().isEmpty() ||
      (failure_preview_retry_button != nullptr &&
       failure_preview_retry_button->property("visible").toBool()) ||
      recovered_layer_failed ||
      !recovered_mask_signature.contains(QStringLiteral("failed:false"))) {
    std::cerr << "Failure recovery row state: stage="
              << editor_preview.previewFailureStage().toStdString()
              << " layerStillFailed=" << recovered_layer_failed
              << " maskSignature=" << recovered_mask_signature.toStdString() << "\n";
    return fail("successful preview recovery should clear affected layer and mask row failures");
  }
  struct ShellFailureCase {
    QString failure_stage;
    QString dirty_domain;
    QStringList dirty_stages;
    QString expected_stage;
    QString expected_status;
    QString expected_row_label;
    QString detail;
  };
  const ShellFailureCase shell_failure_cases[] = {
      {
          QStringLiteral("outline_trace"),
          QStringLiteral("overlay_composite"),
          {QStringLiteral("outline_trace")},
          QStringLiteral("outline_trace"),
          QStringLiteral("Preview failed: Outline trace."),
          QStringLiteral("Preview failed: Outline trace"),
          QStringLiteral("sha1:desktop-smoke-outline-trace"),
      },
      {
          QString(),
          QStringLiteral("export_preview"),
          {QStringLiteral("export_preview")},
          QStringLiteral("export_preview"),
          QStringLiteral("Preview failed: Export preview."),
          QStringLiteral("Preview failed: Export preview"),
          QStringLiteral("sha1:desktop-smoke-export-preview"),
      },
      {
          QString(),
          QStringLiteral("inpaint_patch"),
          {QStringLiteral("retouch_patch_application")},
          QStringLiteral("retouch_patch"),
          QStringLiteral("Preview failed: Retouch patch."),
          QStringLiteral("Preview failed: Retouch patch"),
          QStringLiteral("sha1:desktop-smoke-inpaint-patch"),
      },
      {
          QString(),
          QString(),
          {},
          QStringLiteral("render_pipeline"),
          QStringLiteral("Preview failed: Render pipeline."),
          QStringLiteral("Preview failed: Render pipeline"),
          QStringLiteral("desktop-smoke-no-stage-fallback"),
      },
  };
  for (const auto& failure_case : shell_failure_cases) {
    const auto staged_last_good_preview_source = editor_preview.displayPreviewSource();
    photo_editor::desktop::RenderResult staged_failure;
    staged_failure.file_path = current_file_path;
    staged_failure.generation =
        photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(editor_preview);
    staged_failure.success = false;
    staged_failure.failure_stage = failure_case.failure_stage;
    staged_failure.failure_detail = failure_case.detail;
    staged_failure.failure_layer_ids = QStringList{failure_layer_id};
    staged_failure.failure_mask_set_ids = QStringList{failure_mask_set_id};
    staged_failure.render_reason = QStringLiteral("desktop_smoke_stage_failure");
    staged_failure.render_mode = QStringLiteral("settled_preview");
    staged_failure.interaction_mode = QStringLiteral("interactive");
    staged_failure.dirty_domain = failure_case.dirty_domain;
    staged_failure.dirty_stages = failure_case.dirty_stages;
    staged_failure.active_target = QStringLiteral("mask");
    staged_failure.active_layer_id = failure_layer_id;
    staged_failure.active_mask_set_id = failure_mask_set_id;
    photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
        editor_preview,
        false,
        staged_failure);
    process_layout_events(6);

    QString staged_layer_signature;
    QString staged_layer_state_text;
    for (const auto* layer_row_item :
         find_named_items(edit_workspace, QStringLiteral("photoLayerRow"))) {
      const auto row_signature = layer_row_item->property("layerFailureSignature").toString();
      if (row_signature.contains(QStringLiteral("failed:true")) &&
          row_signature.contains(QStringLiteral("stage:%1").arg(failure_case.expected_stage)) &&
          row_signature.contains(failure_layer_id) &&
          row_signature.contains(failure_mask_set_id)) {
        staged_layer_signature = row_signature;
        const auto row_labels =
            layer_row_item->findChildren<QObject*>(QStringLiteral("photoLayerStateLabel"));
        if (!row_labels.isEmpty()) {
          staged_layer_state_text = row_labels.constFirst()->property("text").toString();
        }
        break;
      }
    }
    photo_layer_nested_mask_row =
        find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskRow"));
    photo_layer_nested_mask_status_label =
        find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskStatusLabel"));
    failure_preview_retry_button =
        find_named_object(edit_workspace, QStringLiteral("editImageViewportPreviewRetryButton"));
    failure_preview_update_badge =
        find_named_object(edit_workspace, QStringLiteral("editImageViewportPreviewUpdateBadge"));
    const auto staged_mask_signature =
        photo_layer_nested_mask_row == nullptr
            ? QString()
            : photo_layer_nested_mask_row->property("maskFailureSignature").toString();
    const auto staged_mask_status =
        photo_layer_nested_mask_status_label == nullptr
            ? QString()
            : photo_layer_nested_mask_status_label->property("text").toString();
    if (editor_preview.status() != failure_case.expected_status ||
        editor_preview.previewFailureStage() != failure_case.expected_stage ||
        !editor_preview.previewFailureDetail().contains(failure_case.detail) ||
        editor_preview.displayPreviewSource() != staged_last_good_preview_source ||
        failure_preview_retry_button == nullptr ||
        !failure_preview_retry_button->property("visible").toBool() ||
        failure_preview_update_badge == nullptr ||
        !failure_preview_update_badge->property("visible").toBool() ||
        staged_layer_signature.trimmed().isEmpty() ||
        !staged_layer_state_text.contains(failure_case.expected_row_label) ||
        !staged_mask_signature.contains(QStringLiteral("failed:true")) ||
        !staged_mask_signature.contains(QStringLiteral("stage:%1").arg(failure_case.expected_stage)) ||
        !staged_mask_status.contains(failure_case.expected_row_label)) {
      std::cerr << "Shell staged failure mismatch: expectedStage="
                << failure_case.expected_stage.toStdString()
                << " status=" << editor_preview.status().toStdString()
                << " stage=" << editor_preview.previewFailureStage().toStdString()
                << " detail=" << editor_preview.previewFailureDetail().toStdString()
                << " layerSignature=" << staged_layer_signature.toStdString()
                << " layerState=" << staged_layer_state_text.toStdString()
                << " maskSignature=" << staged_mask_signature.toStdString()
                << " maskStatus=" << staged_mask_status.toStdString()
                << "\n";
      return fail("shell preview failure should mark affected layer and mask rows for every covered stage");
    }

    last_good_result.generation =
        photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(editor_preview);
    last_good_result.render_reason = QStringLiteral("desktop_smoke_stage_failure_recovery");
    last_good_result.display_preview_source = staged_last_good_preview_source;
    photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
        editor_preview,
        false,
        last_good_result);
    process_layout_events(6);
    auto stage_recovered_layer_failed = false;
    for (const auto* layer_row_item :
         find_named_items(edit_workspace, QStringLiteral("photoLayerRow"))) {
      if (layer_row_item->property("layerFailureSignature")
              .toString()
              .contains(QStringLiteral("failed:true"))) {
        stage_recovered_layer_failed = true;
        break;
      }
    }
    photo_layer_nested_mask_row =
        find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskRow"));
    const auto stage_recovered_mask_signature =
        photo_layer_nested_mask_row == nullptr
            ? QString()
            : photo_layer_nested_mask_row->property("maskFailureSignature").toString();
    if (!editor_preview.previewFailureStage().isEmpty() ||
        !editor_preview.previewFailureLayerIds().isEmpty() ||
        !editor_preview.previewFailureMaskSetIds().isEmpty() ||
        stage_recovered_layer_failed ||
        !stage_recovered_mask_signature.contains(QStringLiteral("failed:false"))) {
      return fail("shell staged failure recovery should clear affected layer and mask row failures");
    }
  }
  const auto layer_thumbnail_data = editor_preview.photoLayers();
  if (layer_thumbnail_data.size() != editor_preview.photoLayerCount()) {
    return fail("photo layer thumbnail proof should inspect the normalized layer model");
  }
  for (const auto& value : layer_thumbnail_data) {
    const auto layer = value.toMap();
    const auto thumbnail_source = layer.value(QStringLiteral("thumbnailSource")).toString();
    if (!layer.value(QStringLiteral("thumbnailReady")).toBool() ||
        !thumbnail_source.startsWith(QStringLiteral("data:image/png;base64,")) ||
        !layer.value(QStringLiteral("thumbnailSignature")).toString().contains(
            QStringLiteral("ready:true")) ||
        layer.value(QStringLiteral("thumbnailBackingWidth")).toInt() < 64 ||
        layer.value(QStringLiteral("thumbnailBackingHeight")).toInt() < 64 ||
        layer.value(QStringLiteral("thumbnailDisplayWidth")).toInt() != 32 ||
        layer.value(QStringLiteral("thumbnailDisplayHeight")).toInt() != 32 ||
        layer.value(QStringLiteral("thumbnailDevicePixelRatio")).toDouble() < 2.0 ||
        !layer.value(QStringLiteral("thumbnailPixelRatioSignature"))
             .toString()
             .contains(QStringLiteral("display:32x32"))) {
      std::cerr << "Layer thumbnail missing: name="
                << layer.value(QStringLiteral("displayName")).toString().toStdString()
                << " kind=" << layer.value(QStringLiteral("kind")).toString().toStdString()
                << " ready=" << layer.value(QStringLiteral("thumbnailReady")).toBool()
                << " source=" << thumbnail_source.left(24).toStdString()
                << " signature="
                << layer.value(QStringLiteral("thumbnailSignature")).toString().toStdString()
                << " pixelSignature="
                << layer.value(QStringLiteral("thumbnailPixelRatioSignature"))
                       .toString()
                       .toStdString()
                << "\n";
      return fail("every normalized photo layer should expose a renderable thumbnail source");
    }
  }
  for (const auto* layer_row_item : photo_layer_rows) {
    if (item_visible(layer_row_item) &&
        scene_rect(layer_row_item).bottom() > base_image_layer_rect.top() + 1.0) {
      return fail("locked Base Image row should stay below every editable photo layer row");
    }
  }
  const auto state_layer_index = photo_layer_row->property("layerIndex").toInt();
  if (!editor_preview.setPhotoLayerEnabled(state_layer_index, false)) {
    return fail("layer state proof should be able to hide the active layer");
  }
  process_layout_events(4);
  photo_layer_row = find_named_object(edit_workspace, QStringLiteral("photoLayerRow"));
  photo_layer_state_label = find_named_object(edit_workspace, QStringLiteral("photoLayerStateLabel"));
  if (photo_layer_row == nullptr || photo_layer_state_label == nullptr ||
      !photo_layer_row->property("layerHidden").toBool() ||
      photo_layer_row->property("layerVisible").toBool() ||
      !photo_layer_row->property("layerStateSignature").toString().contains(
          QStringLiteral("hidden:true")) ||
      !photo_layer_state_label->property("text").toString().contains(QStringLiteral("Hidden"))) {
    return fail("layer rows should expose a clear hidden state");
  }
  if (!editor_preview.setPhotoLayerEnabled(state_layer_index, true) ||
      !editor_preview.setPhotoLayerLocked(state_layer_index, true)) {
    return fail("layer state proof should be able to lock the active layer");
  }
  process_layout_events(4);
  photo_layer_row = find_named_object(edit_workspace, QStringLiteral("photoLayerRow"));
  photo_layer_state_label = find_named_object(edit_workspace, QStringLiteral("photoLayerStateLabel"));
  if (photo_layer_row == nullptr || photo_layer_state_label == nullptr ||
      !photo_layer_row->property("layerLocked").toBool() ||
      !photo_layer_row->property("layerStateSignature").toString().contains(
          QStringLiteral("locked:true")) ||
      !photo_layer_state_label->property("text").toString().contains(QStringLiteral("Locked"))) {
    return fail("layer rows should expose a clear locked state");
  }
  if (!editor_preview.setPhotoLayerLocked(state_layer_index, false) ||
      !editor_preview.isolatePhotoLayer(state_layer_index)) {
    return fail("layer state proof should be able to solo the active layer");
  }
  process_layout_events(4);
  photo_layer_row = find_named_object(edit_workspace, QStringLiteral("photoLayerRow"));
  photo_layer_state_label = find_named_object(edit_workspace, QStringLiteral("photoLayerStateLabel"));
  if (photo_layer_row == nullptr || photo_layer_state_label == nullptr ||
      !photo_layer_row->property("layerSoloed").toBool() ||
      !photo_layer_row->property("layerStateSignature").toString().contains(
          QStringLiteral("solo:true")) ||
      !photo_layer_state_label->property("text").toString().contains(QStringLiteral("Solo")) ||
      !editor_preview.clearPhotoLayerSolo()) {
    return fail("layer rows should expose a clear solo state");
  }
  process_layout_events(4);
  QObject* mask_preview_button = photo_layer_nested_mask_preview_button != nullptr
                                     ? photo_layer_nested_mask_preview_button
                                     : photo_layer_mask_set_preview_button;
  if (mask_preview_button == nullptr ||
      !mask_preview_button->property("enabled").toBool() ||
      mask_preview_button->property("active").toBool() ||
      !QMetaObject::invokeMethod(mask_preview_button, "click")) {
    return fail("mask rows should expose a per-mask before/after preview toggle");
  }
  process_layout_events(4);
  auto mask_preview_bypassed = false;
  for (const auto& mask_set_value : editor_preview.maskSets()) {
    if (mask_set_value.toMap().value(QStringLiteral("maskSetPreviewBypassed")).toBool()) {
      mask_preview_bypassed = true;
      break;
    }
  }
  photo_layer_nested_mask_preview_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskPreviewButton"));
  photo_layer_mask_set_preview_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetPreviewButton"));
  mask_preview_button = photo_layer_nested_mask_preview_button != nullptr
                            ? photo_layer_nested_mask_preview_button
                            : photo_layer_mask_set_preview_button;
  if (mask_preview_button == nullptr || !mask_preview_button->property("active").toBool() ||
      !mask_preview_bypassed ||
      !editor_preview.status().contains(QStringLiteral("before mask attachment"))) {
    return fail("mask before preview should flag only the mask attachment in the layer UI");
  }
  if (!QMetaObject::invokeMethod(mask_preview_button, "click")) {
    return fail("mask before/after preview toggle should restore the mask attachment");
  }
  process_layout_events(4);
  auto mask_preview_restored = true;
  for (const auto& mask_set_value : editor_preview.maskSets()) {
    if (mask_set_value.toMap().value(QStringLiteral("maskSetPreviewBypassed")).toBool()) {
      mask_preview_restored = false;
      break;
    }
  }
  photo_layer_nested_mask_preview_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskPreviewButton"));
  photo_layer_mask_set_preview_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetPreviewButton"));
  mask_preview_button = photo_layer_nested_mask_preview_button != nullptr
                            ? photo_layer_nested_mask_preview_button
                            : photo_layer_mask_set_preview_button;
  if (mask_preview_button == nullptr || mask_preview_button->property("active").toBool() ||
      !mask_preview_restored ||
      !editor_preview.status().contains(QStringLiteral("with mask attachment"))) {
    return fail("mask before/after preview should restore the mask attachment without clearing it");
  }
  process_layout_events(4);
  photo_layer_nested_mask_edit_layer_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskEditLayerButton"));
  photo_layer_mask_set_adjust_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetAdjustButton"));
  QObject* mask_edit_layer_button =
      photo_layer_nested_mask_edit_layer_button != nullptr ? photo_layer_nested_mask_edit_layer_button
                                                          : photo_layer_mask_set_adjust_button;
  if (!QMetaObject::invokeMethod(mask_edit_layer_button, "click")) {
    return fail("mask rows should expose an explicit linked adjustment-layer edit action");
  }
  process_layout_events(4);
  if (edit_workspace->property("editInspectorTab").toString() != QStringLiteral("adjust") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
      active_edit_target_bar->property("targetScope").toString() !=
          QStringLiteral("adjustment-layer") ||
      active_edit_target_mode_label->property("text").toString() !=
          QStringLiteral("Editing Adjustment Layer")) {
    return fail("mask edit-layer buttons should switch to adjustment-layer editing instead of leaving the mask as the active target");
  }
  photo_layer_mask_set_select_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetSelectButton"));
  photo_layer_nested_mask_select_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskSelectButton"));
  QObject* mask_select_button = photo_layer_nested_mask_select_button != nullptr
                                    ? photo_layer_nested_mask_select_button
                                    : photo_layer_mask_set_select_button;
  if (!QMetaObject::invokeMethod(mask_select_button, "click")) {
    return fail("mask rows should expose a mask selection action");
  }
  process_layout_events(4);
  if (edit_workspace->property("editInspectorTab").toString() != QStringLiteral("layers") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("masks") ||
      active_edit_target_bar->property("targetScope").toString() != QStringLiteral("mask") ||
      active_edit_target_mode_label->property("text").toString() != QStringLiteral("Editing Mask")) {
    return fail("clicking a mask row should switch the canvas/tools to mask editing rather than color editing");
  }
  bool found_mask_selected_layer_state = false;
  const auto mask_selected_layer_rows = find_named_items(edit_workspace, QStringLiteral("photoLayerRow"));
  for (auto* row : mask_selected_layer_rows) {
    if (row->property("layerMaskSelected").toBool() &&
        row->property("layerStateSignature").toString().contains(
            QStringLiteral("mask-selected:true"))) {
      found_mask_selected_layer_state = true;
      break;
    }
  }
  if (!found_mask_selected_layer_state) {
    return fail("layer rows should expose a clear mask-selected state when one of their child masks is active");
  }
  photo_layer_nested_mask_select_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskSelectButton"));
  if (!QMetaObject::invokeMethod(photo_layer_nested_mask_select_button, "click")) {
    return fail("mask sets should render as selectable child attachments inside their adjustment layer row");
  }
  process_layout_events(4);
  photo_layer_nested_mask_select_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskSelectButton"));
  if (edit_workspace->property("editInspectorTab").toString() != QStringLiteral("layers") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("masks") ||
      photo_layer_nested_mask_select_button == nullptr ||
      !photo_layer_nested_mask_select_button->property("active").toBool()) {
    return fail("clicking a nested mask child row should switch the canvas to mask editing while keeping Layers visible");
  }
  QVariant layers_target_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activeEditTargetSignature",
          Q_RETURN_ARG(QVariant, layers_target_signature)) ||
      !active_edit_target_bar->property("visible").toBool() ||
      active_edit_target_bar->property("targetScope").toString() != QStringLiteral("mask") ||
      active_edit_target_mode_label->property("text").toString() != QStringLiteral("Editing Mask") ||
      !active_edit_target_breadcrumb_label->property("text").toString().startsWith(
          QStringLiteral("Base Image > Adjustment Layer")) ||
      !layers_target_signature.toString().contains(QStringLiteral("scope:mask")) ||
      !layers_target_signature.toString().contains(QStringLiteral("selection")) ||
      !layers_target_signature.toString().contains(QStringLiteral("Overlay"))) {
    std::cerr << "Layers target state: scope="
              << active_edit_target_bar->property("targetScope").toString().toStdString()
              << " barVisible=" << active_edit_target_bar->property("visible").toBool()
              << " mode="
              << active_edit_target_mode_label->property("text").toString().toStdString()
              << " breadcrumb="
              << active_edit_target_breadcrumb_label->property("text").toString().toStdString()
              << " status="
              << active_adjustment_layer_target_status_label->property("text").toString().toStdString()
              << " signature=" << layers_target_signature.toString().toStdString()
              << '\n';
    return fail("Layers tab should keep the active layer/mask target breadcrumb visible after selecting a nested mask");
  }
  auto* selected_mask_header =
      find_named_object(edit_workspace, QStringLiteral("selectedMaskHeader"));
  auto* selected_mask_header_name_label =
      find_named_object(edit_workspace, QStringLiteral("selectedMaskHeaderNameLabel"));
  auto* selected_mask_header_mode_label =
      find_named_object(edit_workspace, QStringLiteral("selectedMaskHeaderModeLabel"));
  auto* selected_mask_header_status_label =
      find_named_object(edit_workspace, QStringLiteral("selectedMaskHeaderStatusLabel"));
  auto* selected_mask_header_settings_label =
      find_named_object(edit_workspace, QStringLiteral("selectedMaskHeaderSettingsLabel"));
  auto* selected_mask_header_thumbnail_image =
      find_named_object(edit_workspace, QStringLiteral("selectedMaskHeaderThumbnailImage"));
  const auto selected_mask_header_signature =
      selected_mask_header == nullptr
          ? QString()
          : selected_mask_header->property("headerSignature").toString();
  const auto selected_mask_header_thumbnail_source_size =
      selected_mask_header_thumbnail_image == nullptr
          ? QSize()
          : selected_mask_header_thumbnail_image->property("sourceSize").toSize();
  if (selected_mask_header == nullptr || selected_mask_header_name_label == nullptr ||
      selected_mask_header_mode_label == nullptr || selected_mask_header_status_label == nullptr ||
      selected_mask_header_settings_label == nullptr ||
      selected_mask_header_thumbnail_image == nullptr ||
      !item_visible(qobject_cast<QQuickItem*>(selected_mask_header)) ||
      !selected_mask_header_signature.contains(QStringLiteral("selected-mask-header:v1")) ||
      !selected_mask_header_signature.contains(QStringLiteral("|mask:")) ||
      !selected_mask_header_signature.contains(QStringLiteral("|layer:")) ||
      !selected_mask_header_signature.contains(QStringLiteral("|operation:")) ||
      !selected_mask_header_signature.contains(QStringLiteral("|overlay:")) ||
      !selected_mask_header_signature.contains(QStringLiteral("|feather:")) ||
      !selected_mask_header_signature.contains(QStringLiteral("|density:")) ||
      !selected_mask_header_signature.contains(QStringLiteral("|invert:")) ||
      selected_mask_header_mode_label->property("text").toString() !=
          QStringLiteral("Editing Mask") ||
      selected_mask_header_name_label->property("text").toString().trimmed().isEmpty() ||
      !selected_mask_header_status_label->property("text").toString().contains(
          QStringLiteral(" / ")) ||
      !selected_mask_header_settings_label->property("text").toString().contains(
          QStringLiteral("Feather ")) ||
      !selected_mask_header_settings_label->property("text").toString().contains(
          QStringLiteral("Density ")) ||
      !selected_mask_header_settings_label->property("text").toString().contains(
          QStringLiteral("Invert ")) ||
      selected_mask_header_thumbnail_source_size.width() <
          selected_mask_header->property("thumbnailBackingWidth").toInt() ||
      selected_mask_header_thumbnail_source_size.height() <
          selected_mask_header->property("thumbnailBackingHeight").toInt()) {
    std::cerr << "Selected mask header state: header="
              << (selected_mask_header == nullptr
                      ? std::string("null")
                      : selected_mask_header_signature.toStdString())
              << " name="
              << (selected_mask_header_name_label == nullptr
                      ? std::string("null")
                      : selected_mask_header_name_label->property("text").toString().toStdString())
              << " mode="
              << (selected_mask_header_mode_label == nullptr
                      ? std::string("null")
                      : selected_mask_header_mode_label->property("text").toString().toStdString())
              << " status="
              << (selected_mask_header_status_label == nullptr
                      ? std::string("null")
                      : selected_mask_header_status_label->property("text").toString().toStdString())
              << " settings="
              << (selected_mask_header_settings_label == nullptr
                      ? std::string("null")
                      : selected_mask_header_settings_label->property("text").toString().toStdString())
              << '\n';
    return fail("selected mask header should persistently show the active mask, parent layer, operation, overlay, feather, density, and invert state");
  }
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  process_layout_events(4);
  auto* active_adjustment_target_guard =
      find_named_object(edit_workspace, QStringLiteral("activeAdjustmentTargetGuard"));
  auto* active_adjustment_target_guard_title =
      find_named_object(edit_workspace, QStringLiteral("activeAdjustmentTargetGuardTitleLabel"));
  auto* active_adjustment_target_guard_status =
      find_named_object(edit_workspace, QStringLiteral("activeAdjustmentTargetGuardStatusLabel"));
  const auto mask_target_guard_signature =
      active_adjustment_target_guard == nullptr
          ? QString()
          : active_adjustment_target_guard->property("guardSignature").toString();
  if (active_adjustment_target_guard == nullptr ||
      active_adjustment_target_guard_title == nullptr ||
      active_adjustment_target_guard_status == nullptr ||
      !item_visible(qobject_cast<QQuickItem*>(active_adjustment_target_guard)) ||
      !mask_target_guard_signature.contains(QStringLiteral("target-guard:v1")) ||
      !mask_target_guard_signature.contains(QStringLiteral("scope:mask")) ||
      !mask_target_guard_signature.contains(QStringLiteral("global-controls:disabled")) ||
      !mask_target_guard_signature.contains(QStringLiteral("mask-controls:enabled")) ||
      !mask_target_guard_signature.contains(QStringLiteral("color-controls:hidden")) ||
      active_adjustment_target_guard_title->property("text").toString() !=
          QStringLiteral("Editing Mask") ||
      !active_adjustment_target_guard_status->property("text").toString().contains(
          QStringLiteral("color controls are hidden"))) {
    std::cerr << "Mask target guard state: signature="
              << mask_target_guard_signature.toStdString()
              << " title="
              << (active_adjustment_target_guard_title == nullptr
                      ? std::string("null")
                      : active_adjustment_target_guard_title->property("text")
                            .toString()
                            .toStdString())
              << " status="
              << (active_adjustment_target_guard_status == nullptr
                      ? std::string("null")
                      : active_adjustment_target_guard_status->property("text")
                            .toString()
                            .toStdString())
              << '\n';
    return fail("mask target guard should relabel hidden color controls and expose mask-only controls");
  }
  auto* mask_first_inspector_group =
      find_named_object(edit_workspace, QStringLiteral("maskFirstInspectorGroup"));
  auto* mask_first_overlay_button =
      find_named_object(edit_workspace, QStringLiteral("maskFirstOverlayButton"));
  auto* mask_first_outline_button =
      find_named_object(edit_workspace, QStringLiteral("maskFirstOutlineButton"));
  auto* mask_first_hidden_button =
      find_named_object(edit_workspace, QStringLiteral("maskFirstHiddenButton"));
  auto* mask_first_edit_layer_button =
      find_named_object(edit_workspace, QStringLiteral("maskFirstEditLayerButton"));
  auto* mask_first_visibility_button =
      find_named_object(edit_workspace, QStringLiteral("maskFirstVisibilityButton"));
  auto* mask_first_invert_button =
      find_named_object(edit_workspace, QStringLiteral("maskFirstInvertButton"));
  auto* mask_first_feather_slider =
      find_named_object(edit_workspace, QStringLiteral("maskFirstFeatherSlider"));
  auto* mask_first_density_slider =
      find_named_object(edit_workspace, QStringLiteral("maskFirstDensitySlider"));
  auto* mask_first_opacity_slider =
      find_named_object(edit_workspace, QStringLiteral("maskFirstOpacitySlider"));
  auto* mask_first_refine_group =
      find_named_object(edit_workspace, QStringLiteral("maskFirstRefineGroup"));
  auto* mask_first_brush_settings_group =
      find_named_object(edit_workspace, QStringLiteral("maskFirstBrushSettingsGroup"));
  auto* mask_hidden_white_balance_group =
      find_named_object(edit_workspace, QStringLiteral("whiteBalanceInspectorGroup"));
  auto* mask_hidden_light_group =
      find_named_object(edit_workspace, QStringLiteral("lightInspectorGroup"));
  auto* mask_hidden_color_group =
      find_named_object(edit_workspace, QStringLiteral("colorInspectorGroup"));
  auto* mask_hidden_tone_curve_group =
      find_named_object(edit_workspace, QStringLiteral("toneCurveInspectorGroup"));
  const auto mask_first_signature =
      mask_first_inspector_group == nullptr
          ? QString()
          : mask_first_inspector_group->property("maskControlContract").toString();
  if (mask_first_inspector_group == nullptr || mask_first_overlay_button == nullptr ||
      mask_first_outline_button == nullptr || mask_first_hidden_button == nullptr ||
      mask_first_edit_layer_button == nullptr ||
      mask_first_visibility_button == nullptr || mask_first_invert_button == nullptr ||
      mask_first_feather_slider == nullptr || mask_first_density_slider == nullptr ||
      mask_first_opacity_slider == nullptr || mask_first_refine_group == nullptr ||
      mask_first_brush_settings_group == nullptr ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_inspector_group)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_overlay_button)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_outline_button)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_hidden_button)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_edit_layer_button)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_visibility_button)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_invert_button)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_feather_slider)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_density_slider)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_opacity_slider)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_refine_group)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_brush_settings_group)) ||
      !mask_first_signature.contains(QStringLiteral("active:true")) ||
      !mask_first_signature.contains(QStringLiteral("|mode:")) ||
      !mask_first_signature.contains(QStringLiteral("|visible:")) ||
      !mask_first_signature.contains(QStringLiteral("|inverted:")) ||
      !mask_first_signature.contains(QStringLiteral("|feather:")) ||
      !mask_first_signature.contains(QStringLiteral("|density:")) ||
      !mask_first_signature.contains(QStringLiteral("|opacity:")) ||
      !mask_first_signature.contains(QStringLiteral("|brush:true")) ||
      item_visible(qobject_cast<QQuickItem*>(mask_hidden_white_balance_group)) ||
      item_visible(qobject_cast<QQuickItem*>(mask_hidden_light_group)) ||
      item_visible(qobject_cast<QQuickItem*>(mask_hidden_color_group)) ||
      item_visible(qobject_cast<QQuickItem*>(mask_hidden_tone_curve_group))) {
    std::cerr << "Mask-first inspector state: signature="
              << mask_first_signature.toStdString()
              << " groupVisible="
              << item_visible(qobject_cast<QQuickItem*>(mask_first_inspector_group))
              << " wbVisible="
              << item_visible(qobject_cast<QQuickItem*>(mask_hidden_white_balance_group))
              << " lightVisible="
              << item_visible(qobject_cast<QQuickItem*>(mask_hidden_light_group))
              << " colorVisible="
              << item_visible(qobject_cast<QQuickItem*>(mask_hidden_color_group))
              << " curveVisible="
              << item_visible(qobject_cast<QQuickItem*>(mask_hidden_tone_curve_group))
              << '\n';
    return fail("mask edit target should show mask overlay/refine/feather/density/brush controls first and hide color-only adjustment groups");
  }
  if (!QMetaObject::invokeMethod(mask_first_edit_layer_button, "click")) {
    return fail("mask-first inspector should expose a direct linked-layer edit action");
  }
  process_layout_events(4);
  if (edit_workspace->property("editInspectorTab").toString() != QStringLiteral("adjust") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
      active_edit_target_bar->property("targetScope").toString() !=
          QStringLiteral("adjustment-layer") ||
      active_edit_target_mode_label->property("text").toString() !=
          QStringLiteral("Editing Adjustment Layer")) {
    std::cerr << "Mask-to-layer switch state: tab="
              << edit_workspace->property("editInspectorTab").toString().toStdString()
              << " tool=" << edit_workspace->property("editTool").toString().toStdString()
              << " scope="
              << active_edit_target_bar->property("targetScope").toString().toStdString()
              << " mode="
              << active_edit_target_mode_label->property("text").toString().toStdString()
              << '\n';
    return fail("mask Edit Layer should switch to the linked adjustment layer without leaving the Adjust panel");
  }
  auto* active_layer_mask_child_edit_mask_button =
      find_named_object(edit_workspace, QStringLiteral("activeLayerMaskChildEditMaskButton"));
  if (active_layer_mask_child_edit_mask_button == nullptr ||
      !item_visible(qobject_cast<QQuickItem*>(active_layer_mask_child_edit_mask_button)) ||
      !QMetaObject::invokeMethod(active_layer_mask_child_edit_mask_button, "click")) {
    return fail("layer-target linked mask row should expose one-click mask editing in the current Adjust panel");
  }
  process_layout_events(4);
  if (edit_workspace->property("editInspectorTab").toString() != QStringLiteral("adjust") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("masks") ||
      active_edit_target_bar->property("targetScope").toString() != QStringLiteral("mask") ||
      active_edit_target_mode_label->property("text").toString() != QStringLiteral("Editing Mask") ||
      !item_visible(qobject_cast<QQuickItem*>(
          find_named_object(edit_workspace, QStringLiteral("maskFirstInspectorGroup"))))) {
    std::cerr << "Layer-to-mask switch state: tab="
              << edit_workspace->property("editInspectorTab").toString().toStdString()
              << " tool=" << edit_workspace->property("editTool").toString().toStdString()
              << " scope="
              << active_edit_target_bar->property("targetScope").toString().toStdString()
              << " mode="
              << active_edit_target_mode_label->property("text").toString().toStdString()
              << '\n';
    return fail("linked mask Edit Mask should switch back to mask editing without leaving the Adjust panel");
  }
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
  process_layout_events(4);
  if (!photo_layer_text_editor->property("visible").toBool() ||
      photo_layer_text_content_field->property("text").toString() != QStringLiteral("Layer") ||
      !photo_layer_text_bold_button->property("enabled").toBool() ||
      !photo_layer_text_align_center_button->property("enabled").toBool()) {
    return fail("text layers should expose editable content and style controls in the Layers inspector");
  }
  const auto layer_icon_button_accessibility_ready =
      photo_layer_clip_button->property("accessibilityReady").toBool() &&
      photo_layer_mask_button->property("accessibilityReady").toBool() &&
      photo_layer_transform_button->property("accessibilityReady").toBool() &&
      photo_layer_visibility_button->property("accessibilityReady").toBool() &&
      photo_layer_row_solo_button->property("accessibilityReady").toBool() &&
      photo_layer_selection_button->property("accessibilityReady").toBool() &&
      copy_photo_layer_adjustments_button->property("accessibilityReady").toBool() &&
      paste_photo_layer_adjustments_button->property("accessibilityReady").toBool() &&
      paste_global_adjustments_to_photo_layer_button->property("accessibilityReady").toBool() &&
      sync_photo_layer_adjustments_button->property("accessibilityReady").toBool() &&
      clear_photo_layer_selection_button->property("accessibilityReady").toBool() &&
      photo_layer_lock_button->property("accessibilityReady").toBool() &&
      photo_layer_move_up_button->property("accessibilityReady").toBool() &&
      photo_layer_move_down_button->property("accessibilityReady").toBool() &&
      photo_layer_duplicate_button->property("accessibilityReady").toBool() &&
      photo_layer_delete_button->property("accessibilityReady").toBool();
  if (!layer_icon_button_accessibility_ready ||
      photo_layer_clip_button->property("resolvedAccessibleName").toString().isEmpty() ||
      photo_layer_mask_button->property("resolvedAccessibleName").toString().isEmpty() ||
      photo_layer_transform_button->property("resolvedAccessibleName").toString().isEmpty() ||
      photo_layer_visibility_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Toggle layer visibility.") ||
      photo_layer_row_solo_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Show only this layer.") ||
      photo_layer_selection_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Select this layer for layer commands.") ||
      copy_photo_layer_adjustments_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Copy active layer adjustments.") ||
      paste_photo_layer_adjustments_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Paste copied adjustments onto the active layer.") ||
      paste_global_adjustments_to_photo_layer_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Paste Base Image adjustments onto the active layer.") ||
      sync_photo_layer_adjustments_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Sync active layer adjustments to selected adjustment layers.") ||
      clear_photo_layer_selection_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Clear multi-layer selection.") ||
      photo_layer_lock_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Lock layer edits.") ||
      photo_layer_duplicate_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Duplicate layer.") ||
      photo_layer_delete_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Delete layer.")) {
    return fail("icon-only layer controls should expose deterministic accessible names from their tooltips");
  }
  QMetaObject::invokeMethod(photo_layer_name_field, "forceInputFocus");
  photo_layer_name_field->setProperty("text", QStringLiteral("Smoke Renamed"));
  QCoreApplication::processEvents();
  if (editor_preview.photoLayers().at(0).toMap().value(QStringLiteral("name")).toString() !=
      QStringLiteral("Smoke Renamed")) {
    return fail("layer rows should expose a direct editable rename control");
  }
  QMetaObject::invokeMethod(photo_layer_text_content_field, "forceInputFocus");
  photo_layer_text_content_field->setProperty("text", QStringLiteral("Smoke Caption"));
  QMetaObject::invokeMethod(photo_layer_text_font_field, "forceInputFocus");
  photo_layer_text_font_field->setProperty("text", QStringLiteral("Avenir"));
  QMetaObject::invokeMethod(photo_layer_text_color_field, "forceInputFocus");
  photo_layer_text_color_field->setProperty("text", QStringLiteral("#33CCAA"));
  if (!QMetaObject::invokeMethod(photo_layer_text_bold_button, "click") ||
      !QMetaObject::invokeMethod(photo_layer_text_align_center_button, "click")) {
    return fail("text layer style buttons should be clickable from the Layers inspector");
  }
  QCoreApplication::processEvents();
  QMetaObject::invokeMethod(photo_layer_name_field, "clearInputFocus");
  QMetaObject::invokeMethod(photo_layer_text_content_field, "clearInputFocus");
  QMetaObject::invokeMethod(photo_layer_text_font_field, "clearInputFocus");
  QMetaObject::invokeMethod(photo_layer_text_color_field, "clearInputFocus");
  const auto smoked_text_layer = editor_preview.photoLayers().at(0).toMap();
  if (smoked_text_layer.value(QStringLiteral("text")).toString() != QStringLiteral("Smoke Caption") ||
      smoked_text_layer.value(QStringLiteral("fontFamily")).toString() != QStringLiteral("Avenir") ||
      smoked_text_layer.value(QStringLiteral("textColor")).toString() != QStringLiteral("#33ccaa") ||
      smoked_text_layer.value(QStringLiteral("fontWeight")).toString() != QStringLiteral("bold") ||
      smoked_text_layer.value(QStringLiteral("textAlignment")).toString() != QStringLiteral("center")) {
    std::cerr << "Text layer values: text="
              << smoked_text_layer.value(QStringLiteral("text")).toString().toStdString()
              << " font="
              << smoked_text_layer.value(QStringLiteral("fontFamily")).toString().toStdString()
              << " color="
              << smoked_text_layer.value(QStringLiteral("textColor")).toString().toStdString()
              << " weight="
              << smoked_text_layer.value(QStringLiteral("fontWeight")).toString().toStdString()
              << " align="
              << smoked_text_layer.value(QStringLiteral("textAlignment")).toString().toStdString()
              << "\n";
    return fail("text layer inspector controls should update text content, font, color, weight, and alignment");
  }
  if (!editor_preview.addAdjustmentLayer(QStringLiteral("Sync Source")) ||
      !editor_preview.addAdjustmentLayer(QStringLiteral("Sync Target"))) {
    return fail("layer adjustment command smoke setup should create source and target adjustment layers");
  }
  if (!editor_preview.setPhotoLayerAdjustmentValue(1, QStringLiteral("exposure"), 0.64)) {
    return fail("layer adjustment command smoke setup should set source exposure");
  }
  if (!editor_preview.setPhotoLayerAdjustmentValue(1, QStringLiteral("temperature"), -18.0)) {
    return fail("layer adjustment command smoke setup should set source temperature");
  }
  if (!editor_preview.setPhotoLayerAdjustmentValue(0, QStringLiteral("exposure"), -0.25)) {
    return fail("layer adjustment command smoke setup should create source and target adjustment layers");
  }
  editor_preview.setExposure(-0.91);
  QCoreApplication::processEvents();
  QVariant edit_source_layer;
  QVariant source_layer_exposure;
  QVariant edit_layer_target_signature;
  QVariant wrote_source_layer_exposure;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "editPhotoLayerInspectorTarget",
          Q_RETURN_ARG(QVariant, edit_source_layer),
          Q_ARG(QVariant, QVariant(1))) ||
      !edit_source_layer.toBool()) {
    return fail("clicking an adjustment layer should route to an editable layer target");
  }
  process_layout_events(6);
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "primaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, source_layer_exposure),
          Q_ARG(QVariant, QVariant(QStringLiteral("exposure"))),
          Q_ARG(QVariant, QVariant(editor_preview.exposure()))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "activeEditTargetSignature",
          Q_RETURN_ARG(QVariant, edit_layer_target_signature)) ||
      edit_workspace->property("editInspectorTab").toString() != QStringLiteral("adjust") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
      edit_workspace->property("activePhotoLayerIndex").toInt() != 1 ||
      active_edit_target_bar->property("targetScope").toString() !=
          QStringLiteral("adjustment-layer") ||
      active_edit_target_mode_label->property("text").toString() !=
          QStringLiteral("Editing Adjustment Layer") ||
      !active_edit_target_breadcrumb_label->property("text").toString().contains(
          QStringLiteral("Sync Source")) ||
      !edit_layer_target_signature.toString().contains(QStringLiteral("scope:adjustment-layer")) ||
      std::abs(source_layer_exposure.toDouble() - 0.64) > 1e-6 ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setPrimaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, wrote_source_layer_exposure),
          Q_ARG(QVariant, QVariant(QStringLiteral("exposure"))),
          Q_ARG(QVariant, QVariant(0.71))) ||
      !wrote_source_layer_exposure.toBool()) {
    std::cerr << "Layer edit target route state: tab="
              << edit_workspace->property("editInspectorTab").toString().toStdString()
              << " tool=" << edit_workspace->property("editTool").toString().toStdString()
              << " activeLayer=" << edit_workspace->property("activePhotoLayerIndex").toInt()
              << " scope=" << active_edit_target_bar->property("targetScope").toString().toStdString()
              << " mode=" << active_edit_target_mode_label->property("text").toString().toStdString()
              << " breadcrumb="
              << active_edit_target_breadcrumb_label->property("text").toString().toStdString()
              << " primaryExposure=" << source_layer_exposure.toDouble()
              << " signature=" << edit_layer_target_signature.toString().toStdString()
              << "\n";
    return fail("adjustment layer clicks should switch the inspector to that layer's adjustment values");
  }
  active_adjustment_target_guard =
      find_named_object(edit_workspace, QStringLiteral("activeAdjustmentTargetGuard"));
  active_adjustment_target_guard_title =
      find_named_object(edit_workspace, QStringLiteral("activeAdjustmentTargetGuardTitleLabel"));
  active_adjustment_target_guard_status =
      find_named_object(edit_workspace, QStringLiteral("activeAdjustmentTargetGuardStatusLabel"));
  auto* active_layer_full_reset_button =
      find_named_object(edit_workspace, QStringLiteral("activeLayerFullResetButton"));
  const auto layer_target_guard_signature =
      active_adjustment_target_guard == nullptr
          ? QString()
          : active_adjustment_target_guard->property("guardSignature").toString();
  if (active_adjustment_target_guard == nullptr ||
      active_adjustment_target_guard_title == nullptr ||
      active_adjustment_target_guard_status == nullptr ||
      active_layer_full_reset_button == nullptr ||
      !item_visible(qobject_cast<QQuickItem*>(active_adjustment_target_guard)) ||
      !item_visible(qobject_cast<QQuickItem*>(active_layer_full_reset_button)) ||
      !layer_target_guard_signature.contains(QStringLiteral("target-guard:v1")) ||
      !layer_target_guard_signature.contains(QStringLiteral("scope:adjustment-layer")) ||
      !layer_target_guard_signature.contains(QStringLiteral("global-controls:disabled")) ||
      !layer_target_guard_signature.contains(QStringLiteral("layer-controls:enabled")) ||
      !layer_target_guard_signature.contains(QStringLiteral("color-controls:enabled")) ||
      active_adjustment_target_guard_title->property("text").toString() !=
          QStringLiteral("Editing Adjustment Layer") ||
      active_layer_full_reset_button->property("tooltip").toString() !=
          QStringLiteral("Reset the active adjustment layer edits while preserving its mask.") ||
      !active_adjustment_target_guard_status->property("text").toString().contains(
          QStringLiteral("write only to this adjustment layer"))) {
    std::cerr << "Layer target guard state: signature="
              << layer_target_guard_signature.toStdString()
              << " title="
              << (active_adjustment_target_guard_title == nullptr
                      ? std::string("null")
                      : active_adjustment_target_guard_title->property("text")
                            .toString()
                            .toStdString())
              << " status="
              << (active_adjustment_target_guard_status == nullptr
                      ? std::string("null")
                      : active_adjustment_target_guard_status->property("text")
                            .toString()
                            .toStdString())
              << " resetVisible="
              << (active_layer_full_reset_button == nullptr
                      ? std::string("null")
                      : (item_visible(qobject_cast<QQuickItem*>(active_layer_full_reset_button))
                             ? std::string("true")
                             : std::string("false")))
              << '\n';
    return fail("layer target guard should make layer-local controls explicit and keep global controls disabled");
  }
  auto source_adjustment_layer = editor_preview.photoLayers().at(1).toMap();
  auto target_adjustment_layer = editor_preview.photoLayers().at(0).toMap();
  if (std::abs(source_adjustment_layer.value(QStringLiteral("exposure")).toDouble() - 0.71) >
          1e-6 ||
      std::abs(target_adjustment_layer.value(QStringLiteral("exposure")).toDouble() + 0.25) >
          1e-6 ||
      std::abs(editor_preview.exposure() + 0.91) > 1e-6) {
    return fail("editing one clicked adjustment layer should not mutate sibling layers or the base image");
  }
  QVariant edit_target_layer;
  QVariant target_layer_exposure;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "editPhotoLayerInspectorTarget",
          Q_RETURN_ARG(QVariant, edit_target_layer),
          Q_ARG(QVariant, QVariant(0))) ||
      !edit_target_layer.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "primaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, target_layer_exposure),
          Q_ARG(QVariant, QVariant(QStringLiteral("exposure"))),
          Q_ARG(QVariant, QVariant(editor_preview.exposure()))) ||
      edit_workspace->property("activePhotoLayerIndex").toInt() != 0 ||
      !active_edit_target_breadcrumb_label->property("text").toString().contains(
          QStringLiteral("Sync Target")) ||
      std::abs(target_layer_exposure.toDouble() + 0.25) > 1e-6) {
    return fail("switching between adjustment layers should immediately swap inspector values");
  }
  if (!editor_preview.setPhotoLayerAdjustmentValue(1, QStringLiteral("exposure"), 0.64)) {
    return fail("layer edit target route proof should restore the copy/paste source fixture");
  }
  edit_workspace->setProperty("editTool", QStringLiteral("layers"));
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
  QVariant target_selected;
  QVariant source_selected;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, target_selected),
          Q_ARG(QVariant, QVariant(0)),
          Q_ARG(QVariant, QVariant(false))) ||
      !target_selected.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, source_selected),
          Q_ARG(QVariant, QVariant(1)),
          Q_ARG(QVariant, QVariant(true))) ||
      !source_selected.toBool()) {
    return fail("Studio Layers should expose a callable selected-layer target model");
  }
  process_layout_events(6);
  photo_layer_selection_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerSelectionButton"));
  photo_layer_adjustment_command_bar =
      find_named_object(edit_workspace, QStringLiteral("photoLayerAdjustmentCommandBar"));
  copy_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("copyPhotoLayerAdjustmentsButton"));
  paste_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("pastePhotoLayerAdjustmentsButton"));
  paste_global_adjustments_to_photo_layer_button =
      find_named_object(edit_workspace, QStringLiteral("pasteGlobalAdjustmentsToPhotoLayerButton"));
  toggle_photo_layer_adjustments_preview_button =
      find_named_object(edit_workspace, QStringLiteral("togglePhotoLayerAdjustmentsPreviewButton"));
  sync_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("syncPhotoLayerAdjustmentsButton"));
  clear_photo_layer_selection_button =
      find_named_object(edit_workspace, QStringLiteral("clearPhotoLayerSelectionButton"));
  auto* photo_layer_adjustment_command_selection_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerAdjustmentCommandSelectionLabel"));
  if (photo_layer_selection_button == nullptr || photo_layer_adjustment_command_bar == nullptr ||
      copy_photo_layer_adjustments_button == nullptr ||
      paste_photo_layer_adjustments_button == nullptr ||
      paste_global_adjustments_to_photo_layer_button == nullptr ||
      toggle_photo_layer_adjustments_preview_button == nullptr ||
      sync_photo_layer_adjustments_button == nullptr ||
      clear_photo_layer_selection_button == nullptr ||
      photo_layer_adjustment_command_selection_label == nullptr ||
      !photo_layer_adjustment_command_bar->property("visible").toBool() ||
      !copy_photo_layer_adjustments_button->property("enabled").toBool() ||
      !paste_global_adjustments_to_photo_layer_button->property("enabled").toBool() ||
      !sync_photo_layer_adjustments_button->property("enabled").toBool() ||
      !clear_photo_layer_selection_button->property("enabled").toBool() ||
      !photo_layer_selection_button->property("active").toBool() ||
      !photo_layer_adjustment_command_selection_label->property("text")
           .toString()
           .contains(QStringLiteral("1 sync target"))) {
    return fail("Studio Layers should expose active-to-selected copy/paste/sync layer adjustment commands");
  }
  if (!QMetaObject::invokeMethod(copy_photo_layer_adjustments_button, "click") ||
      !editor_preview.hasCopiedPhotoLayerAdjustments()) {
    return fail("copy layer adjustments command should copy the active adjustment recipe");
  }
  process_layout_events(4);
  paste_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("pastePhotoLayerAdjustmentsButton"));
  if (paste_photo_layer_adjustments_button == nullptr ||
      !paste_photo_layer_adjustments_button->property("enabled").toBool()) {
    return fail("paste layer adjustments command should enable after a layer recipe is copied");
  }
  QVariant paste_target_selected;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, paste_target_selected),
          Q_ARG(QVariant, QVariant(0)),
          Q_ARG(QVariant, QVariant(false))) ||
      !paste_target_selected.toBool()) {
    return fail("Studio Layers should switch the active paste target to a selected layer");
  }
  process_layout_events(4);
  paste_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("pastePhotoLayerAdjustmentsButton"));
  if (paste_photo_layer_adjustments_button == nullptr ||
      !paste_photo_layer_adjustments_button->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(paste_photo_layer_adjustments_button, "click")) {
    return fail("paste layer adjustments command should dispatch from the Layers inspector");
  }
  process_layout_events(4);
  auto synced_adjustment_layer = editor_preview.photoLayers().at(0).toMap();
  if (std::abs(synced_adjustment_layer.value(QStringLiteral("exposure")).toDouble() - 0.64) >
          1e-6 ||
      std::abs(synced_adjustment_layer.value(QStringLiteral("temperature")).toDouble() + 18.0) >
          1e-6 ||
      !editor_preview.status().contains(QStringLiteral("Pasted layer adjustments"))) {
    return fail("paste layer adjustments command should copy the active recipe to the active target");
  }
  QVariant sync_target_selected;
  QVariant sync_source_selected;
  if (!editor_preview.setPhotoLayerAdjustmentValue(0, QStringLiteral("exposure"), -0.25) ||
      !editor_preview.setPhotoLayerAdjustmentValue(0, QStringLiteral("temperature"), 8.0) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, sync_target_selected),
          Q_ARG(QVariant, QVariant(0)),
          Q_ARG(QVariant, QVariant(false))) ||
      !sync_target_selected.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, sync_source_selected),
          Q_ARG(QVariant, QVariant(1)),
          Q_ARG(QVariant, QVariant(true))) ||
      !sync_source_selected.toBool()) {
    return fail("Studio Layers should restore the active source plus selected sync target");
  }
  process_layout_events(4);
  sync_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("syncPhotoLayerAdjustmentsButton"));
  photo_layer_adjustment_command_selection_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerAdjustmentCommandSelectionLabel"));
  if (sync_photo_layer_adjustments_button == nullptr ||
      photo_layer_adjustment_command_selection_label == nullptr ||
      !sync_photo_layer_adjustments_button->property("enabled").toBool() ||
      !photo_layer_adjustment_command_selection_label->property("text")
           .toString()
           .contains(QStringLiteral("1 sync target")) ||
      !QMetaObject::invokeMethod(sync_photo_layer_adjustments_button, "click")) {
    return fail("sync layer adjustments command should dispatch from the Layers inspector");
  }
  process_layout_events(4);
  synced_adjustment_layer = editor_preview.photoLayers().at(0).toMap();
  if (std::abs(synced_adjustment_layer.value(QStringLiteral("exposure")).toDouble() - 0.64) >
          1e-6 ||
      std::abs(synced_adjustment_layer.value(QStringLiteral("temperature")).toDouble() + 18.0) >
          1e-6 ||
      !editor_preview.status().contains(QStringLiteral("Synced layer adjustments"))) {
    return fail("sync layer adjustments command should copy the active adjustment recipe to selected layers");
  }
  editor_preview.setExposure(0.19);
  editor_preview.setTemperature(14.0);
  process_layout_events(4);
  paste_global_adjustments_to_photo_layer_button =
      find_named_object(edit_workspace, QStringLiteral("pasteGlobalAdjustmentsToPhotoLayerButton"));
  if (paste_global_adjustments_to_photo_layer_button == nullptr ||
      !paste_global_adjustments_to_photo_layer_button->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(paste_global_adjustments_to_photo_layer_button, "click")) {
    return fail("paste-from-global layer command should dispatch from the Layers inspector");
  }
  process_layout_events(4);
  auto global_pasted_adjustment_layer = editor_preview.photoLayers().at(1).toMap();
  const auto global_pasted_group_label =
      global_pasted_adjustment_layer.value(QStringLiteral("changedAdjustmentGroupLabel")).toString();
  if (std::abs(global_pasted_adjustment_layer.value(QStringLiteral("exposure")).toDouble() -
               0.19) > 1e-6 ||
      std::abs(global_pasted_adjustment_layer.value(QStringLiteral("temperature")).toDouble() -
               14.0) > 1e-6 ||
      !global_pasted_adjustment_layer.value(QStringLiteral("hasNonNeutralAdjustments")).toBool() ||
      !global_pasted_group_label.contains(QStringLiteral("WB")) ||
      !global_pasted_group_label.contains(QStringLiteral("Light")) ||
      !editor_preview.status().contains(QStringLiteral("Pasted Base Image adjustments"))) {
    return fail("paste-from-global command should replace the active layer recipe with Base Image adjustments");
  }
  QVariantList badge_lut_stack_table;
  for (int sample_index = 0; sample_index < 8; ++sample_index) {
    badge_lut_stack_table.append(QVariant(QVariantList{0.92, 0.08, 0.18, 1.0}));
  }
  QVariantMap badge_lut_entry;
  badge_lut_entry.insert(QStringLiteral("enabled"), true);
  badge_lut_entry.insert(QStringLiteral("strength"), 72.0);
  badge_lut_entry.insert(QStringLiteral("name"), QStringLiteral("Badge Runtime LUT"));
  badge_lut_entry.insert(QStringLiteral("size"), 2);
  badge_lut_entry.insert(QStringLiteral("table"), badge_lut_stack_table);
  badge_lut_entry.insert(QStringLiteral("domainMin"), QVariantList{0.0, 0.0, 0.0});
  badge_lut_entry.insert(QStringLiteral("domainMax"), QVariantList{1.0, 1.0, 1.0});
  badge_lut_entry.insert(QStringLiteral("inputColorSpace"), QStringLiteral("sRGB"));
  badge_lut_entry.insert(QStringLiteral("outputColorSpace"), QStringLiteral("sRGB"));
  if (!editor_preview.setPhotoLayerAdjustmentValue(1, QStringLiteral("curveShadows"), 18.0) ||
      !editor_preview.setPhotoLayerAdjustmentString(
          1, QStringLiteral("colorMixerModel"), QStringLiteral("HSL")) ||
      !editor_preview.setPhotoLayerAdjustmentString(
          1, QStringLiteral("hslChannel"), QStringLiteral("Reds")) ||
      !editor_preview.setPhotoLayerAdjustmentValue(1, QStringLiteral("hueAdjustment"), -22.0) ||
      !editor_preview.setPhotoLayerAdjustmentValue(1, QStringLiteral("gradingHighlightsHue"), 0.18) ||
      !editor_preview.setPhotoLayerLutStack(1, QVariantList{badge_lut_entry})) {
    return fail("changed group badge proof should seed curve, HSL, grading, and LUT edits on the active adjustment layer");
  }
  process_layout_events(4);
  selected_layer_header_changed_groups_label =
      find_named_object(edit_workspace, QStringLiteral("selectedLayerHeaderChangedGroupsLabel"));
  QQuickItem* runtime_changed_group_layer_row = nullptr;
  for (auto* row : find_named_items(edit_workspace, QStringLiteral("photoLayerRow"))) {
    if (row != nullptr && row->property("layerIndex").toInt() == 1) {
      runtime_changed_group_layer_row = row;
      break;
    }
  }
  const auto runtime_changed_group_signature =
      runtime_changed_group_layer_row == nullptr
          ? QString()
          : runtime_changed_group_layer_row->property("changedAdjustmentGroupSignature").toString();
  const auto runtime_changed_group_summary =
      runtime_changed_group_layer_row == nullptr
          ? QString()
          : runtime_changed_group_layer_row->property("localAdjustmentSummary").toString();
  if (selected_layer_header_changed_groups_label == nullptr ||
      runtime_changed_group_layer_row == nullptr ||
      !item_visible(runtime_changed_group_layer_row) ||
      !runtime_changed_group_signature.contains(QStringLiteral("WB")) ||
      !runtime_changed_group_signature.contains(QStringLiteral("Light")) ||
      !runtime_changed_group_signature.contains(QStringLiteral("Curve")) ||
      !runtime_changed_group_signature.contains(QStringLiteral("HSL")) ||
      !runtime_changed_group_signature.contains(QStringLiteral("Grade")) ||
      !runtime_changed_group_signature.contains(QStringLiteral("LUT")) ||
      !runtime_changed_group_summary.contains(QStringLiteral("Layer edits:")) ||
      !runtime_changed_group_summary.contains(QStringLiteral("Curve")) ||
      !runtime_changed_group_summary.contains(QStringLiteral("HSL Reds")) ||
      !runtime_changed_group_summary.contains(QStringLiteral("Grade")) ||
      !runtime_changed_group_summary.contains(QStringLiteral("LUT")) ||
      !selected_layer_header_changed_groups_label->property("text")
           .toString()
           .contains(QStringLiteral("Layer edits:")) ||
      !selected_layer_header_changed_groups_label->property("text")
           .toString()
           .contains(QStringLiteral("Curve")) ||
      !selected_layer_header_changed_groups_label->property("text")
           .toString()
           .contains(QStringLiteral("HSL Reds")) ||
      !selected_layer_header_changed_groups_label->property("text")
           .toString()
           .contains(QStringLiteral("Grade")) ||
      !selected_layer_header_changed_groups_label->property("text")
           .toString()
           .contains(QStringLiteral("LUT"))) {
    std::cerr << "Changed group badge state: signature="
              << runtime_changed_group_signature.toStdString()
              << " summary=" << runtime_changed_group_summary.toStdString()
              << " header="
              << (selected_layer_header_changed_groups_label == nullptr
                      ? std::string("null")
                      : selected_layer_header_changed_groups_label->property("text")
                            .toString()
                            .toStdString())
              << '\n';
    return fail("edited adjustment layers should show changed group badges and summaries in the layer UI");
  }
  toggle_photo_layer_adjustments_preview_button =
      find_named_object(edit_workspace, QStringLiteral("togglePhotoLayerAdjustmentsPreviewButton"));
  if (toggle_photo_layer_adjustments_preview_button == nullptr ||
      !toggle_photo_layer_adjustments_preview_button->property("enabled").toBool() ||
      toggle_photo_layer_adjustments_preview_button->property("active").toBool() ||
      !QMetaObject::invokeMethod(toggle_photo_layer_adjustments_preview_button, "click")) {
    return fail("edited adjustment layers should expose a before/after preview toggle");
  }
  process_layout_events(4);
  toggle_photo_layer_adjustments_preview_button =
      find_named_object(edit_workspace, QStringLiteral("togglePhotoLayerAdjustmentsPreviewButton"));
  auto preview_bypassed_adjustment_layer = editor_preview.photoLayers().at(1).toMap();
  if (toggle_photo_layer_adjustments_preview_button == nullptr ||
      !toggle_photo_layer_adjustments_preview_button->property("active").toBool() ||
      !preview_bypassed_adjustment_layer.value(QStringLiteral("adjustmentsPreviewBypassed"))
           .toBool() ||
      std::abs(preview_bypassed_adjustment_layer.value(QStringLiteral("exposure")).toDouble() -
               0.19) > 1e-6 ||
      std::abs(preview_bypassed_adjustment_layer.value(QStringLiteral("temperature")).toDouble() -
               14.0) > 1e-6 ||
      !preview_bypassed_adjustment_layer.value(QStringLiteral("hasNonNeutralAdjustments"))
           .toBool() ||
      !preview_bypassed_adjustment_layer.value(QStringLiteral("layerAuditLabel"))
           .toString()
           .contains(QStringLiteral("preview: before")) ||
      !editor_preview.status().contains(QStringLiteral("Showing layer before adjustments"))) {
    return fail("layer before/after preview should bypass only the rendered adjustment recipe");
  }
  if (!QMetaObject::invokeMethod(toggle_photo_layer_adjustments_preview_button, "click")) {
    return fail("layer before/after preview toggle should restore the adjusted preview");
  }
  process_layout_events(4);
  toggle_photo_layer_adjustments_preview_button =
      find_named_object(edit_workspace, QStringLiteral("togglePhotoLayerAdjustmentsPreviewButton"));
  auto preview_restored_adjustment_layer = editor_preview.photoLayers().at(1).toMap();
  if (toggle_photo_layer_adjustments_preview_button == nullptr ||
      toggle_photo_layer_adjustments_preview_button->property("active").toBool() ||
      preview_restored_adjustment_layer.value(QStringLiteral("adjustmentsPreviewBypassed"))
          .toBool() ||
      std::abs(preview_restored_adjustment_layer.value(QStringLiteral("exposure")).toDouble() -
               0.19) > 1e-6 ||
      std::abs(preview_restored_adjustment_layer.value(QStringLiteral("temperature")).toDouble() -
               14.0) > 1e-6 ||
      !editor_preview.status().contains(QStringLiteral("Showing layer with adjustments"))) {
    return fail("layer before/after preview should restore without changing the adjustment recipe");
  }
  if (!QMetaObject::invokeMethod(clear_photo_layer_selection_button, "click")) {
    return fail("clear layer selection command should be clickable");
  }
  process_layout_events(4);
  clear_photo_layer_selection_button =
      find_named_object(edit_workspace, QStringLiteral("clearPhotoLayerSelectionButton"));
  sync_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("syncPhotoLayerAdjustmentsButton"));
  if (clear_photo_layer_selection_button == nullptr ||
      sync_photo_layer_adjustments_button == nullptr ||
      clear_photo_layer_selection_button->property("enabled").toBool() ||
      sync_photo_layer_adjustments_button->property("enabled").toBool()) {
    return fail("clearing layer selection should disable multi-target sync commands");
  }
  if (!editor_preview.deletePhotoLayer(0) || !editor_preview.deletePhotoLayer(0)) {
    return fail("layer adjustment command smoke cleanup should remove temporary adjustment layers");
  }
  QVariant restored_layer_selected;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, restored_layer_selected),
          Q_ARG(QVariant, QVariant(0)),
          Q_ARG(QVariant, QVariant(false))) ||
      !restored_layer_selected.toBool()) {
    return fail("layer adjustment command smoke cleanup should restore an active editable layer");
  }
  process_layout_events(6);
  photo_layer_lock_button = find_named_object(edit_workspace, QStringLiteral("photoLayerLockButton"));
  photo_layer_move_up_button = find_named_object(edit_workspace, QStringLiteral("photoLayerMoveUpButton"));
  photo_layer_move_down_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMoveDownButton"));
  photo_layer_duplicate_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerDuplicateButton"));
  photo_layer_delete_button = find_named_object(edit_workspace, QStringLiteral("photoLayerDeleteButton"));
  if (photo_layer_lock_button == nullptr || photo_layer_move_up_button == nullptr ||
      photo_layer_move_down_button == nullptr || photo_layer_duplicate_button == nullptr ||
      photo_layer_delete_button == nullptr ||
      photo_layer_move_up_button->property("enabled").toBool() ||
      !photo_layer_move_down_button->property("enabled").toBool() ||
      !photo_layer_duplicate_button->property("enabled").toBool() ||
      !photo_layer_delete_button->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(photo_layer_lock_button, "click")) {
    return fail("layer structure row should expose lock, reorder, duplicate, and delete affordances");
  }
  QCoreApplication::processEvents();
  photo_layer_lock_button = find_named_object(edit_workspace, QStringLiteral("photoLayerLockButton"));
  photo_layer_duplicate_button = find_named_object(edit_workspace, QStringLiteral("photoLayerDuplicateButton"));
  photo_layer_delete_button = find_named_object(edit_workspace, QStringLiteral("photoLayerDeleteButton"));
  photo_layer_name_field = find_named_object(edit_workspace, QStringLiteral("photoLayerNameField"));
  photo_layer_transform_button = find_named_object(edit_workspace, QStringLiteral("photoLayerTransformButton"));
  if (photo_layer_lock_button == nullptr || photo_layer_duplicate_button == nullptr ||
      photo_layer_delete_button == nullptr || photo_layer_name_field == nullptr ||
      photo_layer_transform_button == nullptr ||
      !photo_layer_lock_button->property("active").toBool() ||
      !photo_layer_transform_button->property("active").toBool() ||
      photo_layer_name_field->property("enabled").toBool() ||
      photo_layer_duplicate_button->property("enabled").toBool() ||
      photo_layer_delete_button->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(photo_layer_lock_button, "click")) {
    std::cerr << "Layer lock smoke state: lockButton="
              << (photo_layer_lock_button == nullptr ? "null" : "present")
              << " duplicateButton="
              << (photo_layer_duplicate_button == nullptr ? "null" : "present")
              << " deleteButton="
              << (photo_layer_delete_button == nullptr ? "null" : "present")
              << " nameField="
              << (photo_layer_name_field == nullptr ? "null" : "present")
              << " transformButton="
              << (photo_layer_transform_button == nullptr ? "null" : "present")
              << " lockActive="
              << (photo_layer_lock_button != nullptr &&
                  photo_layer_lock_button->property("active").toBool())
              << " transformActive="
              << (photo_layer_transform_button != nullptr &&
                  photo_layer_transform_button->property("active").toBool())
              << " nameEnabled="
              << (photo_layer_name_field != nullptr &&
                  photo_layer_name_field->property("enabled").toBool())
              << " duplicateEnabled="
              << (photo_layer_duplicate_button != nullptr &&
                  photo_layer_duplicate_button->property("enabled").toBool())
              << " deleteEnabled="
              << (photo_layer_delete_button != nullptr &&
                  photo_layer_delete_button->property("enabled").toBool())
              << " activePhotoLayerIndex="
              << edit_workspace->property("activePhotoLayerIndex").toInt()
              << " layerCount=" << editor_preview.photoLayerCount() << "\n";
    return fail("locking a layer from Studio should block mutation while keeping selection and inspection visible");
  }
  QCoreApplication::processEvents();
  photo_layer_duplicate_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerDuplicateButton"));
  const auto layer_count_before_duplicate = editor_preview.photoLayerCount();
  if (photo_layer_duplicate_button == nullptr ||
      !QMetaObject::invokeMethod(photo_layer_duplicate_button, "click")) {
    return fail("layer duplicate affordance should dispatch to the layer stack controller");
  }
  QCoreApplication::processEvents();
  if (editor_preview.photoLayerCount() != layer_count_before_duplicate + 1) {
    return fail("duplicating a layer from Studio should add one layer");
  }
  photo_layer_delete_button = find_named_object(edit_workspace, QStringLiteral("photoLayerDeleteButton"));
  if (photo_layer_delete_button == nullptr ||
      !QMetaObject::invokeMethod(photo_layer_delete_button, "click")) {
    return fail("layer delete affordance should dispatch to the layer stack controller");
  }
  QCoreApplication::processEvents();
  if (editor_preview.photoLayerCount() != layer_count_before_duplicate) {
    return fail("deleting a layer from Studio should remove one layer and keep the stack valid");
  }
  auto* layer_visibility_state_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerVisibilityStateLabel"));
  auto* save_layer_visibility_state_button =
      find_named_object(edit_workspace, QStringLiteral("saveLayerVisibilityStateButton"));
  auto* apply_layer_visibility_state_button =
      find_named_object(edit_workspace, QStringLiteral("applyLayerVisibilityStateButton"));
  auto* delete_layer_visibility_state_button =
      find_named_object(edit_workspace, QStringLiteral("deleteLayerVisibilityStateButton"));
  if (layer_visibility_state_label == nullptr || save_layer_visibility_state_button == nullptr ||
      apply_layer_visibility_state_button == nullptr || delete_layer_visibility_state_button == nullptr ||
      layer_visibility_state_label->property("text").toString() != QStringLiteral("No visibility states") ||
      !save_layer_visibility_state_button->property("enabled").toBool() ||
      apply_layer_visibility_state_button->property("enabled").toBool() ||
      delete_layer_visibility_state_button->property("enabled").toBool()) {
    return fail("layer panel should expose disabled visibility-state apply/delete controls until a state exists");
  }
  const auto layer_count_before_visibility_save = editor_preview.photoLayerCount();
  const auto expected_visibility_state_summary =
      QStringLiteral("%1/%2 visible")
          .arg(layer_count_before_visibility_save - 1)
          .arg(layer_count_before_visibility_save);
  if (!editor_preview.setPhotoLayerEnabled(0, false) ||
      !editor_preview.savePhotoLayerVisibilityState(QStringLiteral("Smoke Visibility"))) {
    return fail("layer panel smoke setup should save a layer visibility state");
  }
  for (auto iteration = 0; iteration < 4; ++iteration) {
    QCoreApplication::processEvents();
  }
  layer_visibility_state_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerVisibilityStateLabel"));
  apply_layer_visibility_state_button =
      find_named_object(edit_workspace, QStringLiteral("applyLayerVisibilityStateButton"));
  delete_layer_visibility_state_button =
      find_named_object(edit_workspace, QStringLiteral("deleteLayerVisibilityStateButton"));
  if (editor_preview.photoLayerVisibilityStateCount() != 1 ||
      layer_visibility_state_label == nullptr || apply_layer_visibility_state_button == nullptr ||
      delete_layer_visibility_state_button == nullptr ||
      !layer_visibility_state_label->property("text").toString().contains(QStringLiteral("Smoke Visibility")) ||
      !layer_visibility_state_label->property("text").toString().contains(expected_visibility_state_summary) ||
      !apply_layer_visibility_state_button->property("enabled").toBool() ||
      !delete_layer_visibility_state_button->property("enabled").toBool()) {
    return fail("layer panel should show the latest saved visibility state and enable restore/delete controls");
  }
  if (!editor_preview.setPhotoLayerEnabled(0, true) ||
      !editor_preview.applyPhotoLayerVisibilityState(0) ||
      editor_preview.photoLayers().at(0).toMap().value(QStringLiteral("enabled")).toBool() ||
      !editor_preview.deletePhotoLayerVisibilityState(0) ||
      !editor_preview.setPhotoLayerEnabled(0, true)) {
    return fail("layer visibility state controls should restore, delete, and allow returning to the full stack");
  }
  QCoreApplication::processEvents();
  QVariant layer_transform_selected;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayer",
          Q_RETURN_ARG(QVariant, layer_transform_selected),
          Q_ARG(QVariant, 0)) ||
      !layer_transform_selected.toBool() ||
      edit_workspace->property("editInspectorTab").toString() != QStringLiteral("layers")) {
    return fail("layer panel should expose a selectable direct-transform target");
  }
  QObject* photo_layer_transform_overlay = nullptr;
  QObject* photo_layer_transform_frame = nullptr;
  QObject* photo_layer_move_handle = nullptr;
  QObject* photo_layer_rotate_handle = nullptr;
  QObject* photo_layer_scale_handle = nullptr;
  QObject* photo_layer_transform_label = nullptr;
  if (!wait_for_condition([&]() {
        photo_layer_transform_button =
            find_named_object(edit_workspace, QStringLiteral("photoLayerTransformButton"));
        photo_layer_transform_overlay =
            find_named_object(edit_workspace, QStringLiteral("photoLayerTransformOverlay"));
        photo_layer_transform_frame =
            find_named_object(edit_workspace, QStringLiteral("photoLayerTransformFrame"));
        photo_layer_move_handle =
            find_named_object(edit_workspace, QStringLiteral("photoLayerTransformMoveHandle"));
        photo_layer_rotate_handle =
            find_named_object(edit_workspace, QStringLiteral("photoLayerTransformRotateHandle"));
        photo_layer_scale_handle = find_named_object(
            edit_workspace, QStringLiteral("photoLayerTransformBottomRightHandle"));
        photo_layer_transform_label =
            find_named_object(edit_workspace, QStringLiteral("photoLayerTransformLabel"));
        return photo_layer_transform_button != nullptr &&
               photo_layer_transform_overlay != nullptr &&
               photo_layer_transform_frame != nullptr &&
               photo_layer_move_handle != nullptr &&
               photo_layer_rotate_handle != nullptr &&
               photo_layer_scale_handle != nullptr &&
               photo_layer_transform_label != nullptr &&
               photo_layer_transform_overlay->property("active").toBool() &&
               photo_layer_transform_overlay->property("visible").toBool() &&
               photo_layer_transform_button->property("active").toBool();
      })) {
    std::cerr << "Layer transform handles state: button="
              << (photo_layer_transform_button == nullptr ? "null" : "present")
              << " overlay="
              << (photo_layer_transform_overlay == nullptr ? "null" : "present")
              << " frame=" << (photo_layer_transform_frame == nullptr ? "null" : "present")
              << " move=" << (photo_layer_move_handle == nullptr ? "null" : "present")
              << " rotate=" << (photo_layer_rotate_handle == nullptr ? "null" : "present")
              << " scale=" << (photo_layer_scale_handle == nullptr ? "null" : "present")
              << " label=" << (photo_layer_transform_label == nullptr ? "null" : "present")
              << " editTool=" << edit_workspace->property("editTool").toString().toStdString()
              << " activeLayer=" << edit_workspace->property("activePhotoLayerIndex").toInt()
              << "\n";
  }
  if (photo_layer_transform_overlay == nullptr || photo_layer_transform_frame == nullptr ||
      photo_layer_move_handle == nullptr || photo_layer_rotate_handle == nullptr ||
      photo_layer_scale_handle == nullptr || photo_layer_transform_label == nullptr ||
      !photo_layer_transform_overlay->property("active").toBool() ||
      !photo_layer_transform_overlay->property("visible").toBool() ||
      !photo_layer_transform_button->property("active").toBool()) {
    return fail("active photo layers should expose direct on-canvas transform handles");
  }
  const auto transform_scale_x =
      photo_layer_transform_overlay->property("viewportScaleX").toDouble();
  const auto transform_scale_y =
      photo_layer_transform_overlay->property("viewportScaleY").toDouble();
  const auto transform_start = editor_preview.photoLayers().at(0).toMap();
  QVariant transform_moved;
  const auto transform_move_invoked = QMetaObject::invokeMethod(
          photo_layer_transform_overlay,
          "moveByViewportDelta",
          Q_RETURN_ARG(QVariant, transform_moved),
          Q_ARG(QVariant, 24.0),
          Q_ARG(QVariant, 12.0));
  if (transform_scale_x <= 0.0 || transform_scale_y <= 0.0 ||
      !transform_move_invoked || !transform_moved.toBool()) {
    std::cerr << "Layer transform move state: scaleX=" << transform_scale_x
              << " scaleY=" << transform_scale_y
              << " invoked=" << transform_move_invoked
              << " returned=" << transform_moved.toBool()
              << " active=" << photo_layer_transform_overlay->property("active").toBool()
              << " visible=" << photo_layer_transform_overlay->property("visible").toBool()
              << " enabled=" << photo_layer_transform_overlay->property("enabled").toBool()
              << " locked=" << photo_layer_transform_overlay->property("locked").toBool()
              << "\n";
    return fail("layer transform overlay should expose a precise move operation");
  }
  QCoreApplication::processEvents();
  const auto moved_layer = editor_preview.photoLayers().at(0).toMap();
  const auto expected_layer_x =
      qRound(transform_start.value(QStringLiteral("x")).toDouble() + 24.0 / transform_scale_x);
  const auto expected_layer_y =
      qRound(transform_start.value(QStringLiteral("y")).toDouble() + 12.0 / transform_scale_y);
  if (moved_layer.value(QStringLiteral("x")).toInt() != expected_layer_x ||
      moved_layer.value(QStringLiteral("y")).toInt() != expected_layer_y ||
      !photo_layer_transform_label->property("text").toString().contains(QStringLiteral("X "))) {
    return fail("layer transform overlay movement should update exact layer transform metadata");
  }
  if (!editor_preview.setPhotoLayerOpacity(0, 0.42) ||
      !editor_preview.setPhotoLayerBlendMode(0, QStringLiteral("multiply")) ||
      !editor_preview.setPhotoLayerClippingMask(0, true) ||
      !editor_preview.setPhotoLayerMaskEnabled(0, true)) {
    return fail("layer panel smoke setup should update direct layer control state");
  }
  for (auto iteration = 0; iteration < 4; ++iteration) {
    QCoreApplication::processEvents();
  }
  photo_layer_opacity_label = find_named_object(edit_workspace, QStringLiteral("photoLayerOpacityLabel"));
  photo_layer_opacity_slider = find_named_object(edit_workspace, QStringLiteral("photoLayerOpacitySlider"));
  photo_layer_blend_combo = find_named_object(edit_workspace, QStringLiteral("photoLayerBlendModeComboBox"));
  photo_layer_clip_button = find_named_object(edit_workspace, QStringLiteral("photoLayerClippingMaskButton"));
  photo_layer_mask_button = find_named_object(edit_workspace, QStringLiteral("photoLayerMaskButton"));
  photo_layer_mask_badge = find_named_object(edit_workspace, QStringLiteral("photoLayerMaskBadge"));
  if (photo_layer_opacity_label == nullptr || photo_layer_opacity_slider == nullptr ||
      photo_layer_blend_combo == nullptr || photo_layer_clip_button == nullptr ||
      photo_layer_mask_button == nullptr || photo_layer_mask_badge == nullptr) {
    return fail("layer panel controls should remain discoverable after layer stack structure changes");
  }
  if (photo_layer_opacity_label->property("text").toString() != QStringLiteral("42%") ||
      std::abs(photo_layer_opacity_slider->property("value").toDouble() - 42.0) > 0.5 ||
      photo_layer_blend_combo->property("currentText").toString() != QStringLiteral("Multiply") ||
      !photo_layer_clip_button->property("active").toBool() ||
      !photo_layer_mask_button->property("active").toBool() ||
      !photo_layer_mask_badge->property("visible").toBool()) {
    std::cerr << "Layer controls smoke state: opacity="
              << photo_layer_opacity_label->property("text").toString().toStdString()
              << " opacitySlider=" << photo_layer_opacity_slider->property("value").toDouble()
              << " blend=" << photo_layer_blend_combo->property("currentText").toString().toStdString()
              << " clipActive=" << photo_layer_clip_button->property("active").toBool()
              << " maskActive=" << photo_layer_mask_button->property("active").toBool()
              << " maskBadgeVisible=" << photo_layer_mask_badge->property("visible").toBool()
              << "\n";
    return fail("layer panel controls should reflect live opacity, blend, clipping, and mask states");
  }
  if (!editor_preview.isolatePhotoLayer(0)) {
    return fail("layer panel smoke setup should isolate the selected layer");
  }
  QCoreApplication::processEvents();
  if (!editor_preview.photoLayerSoloActive() ||
      photo_layer_solo_button->property("text").toString() != QStringLiteral("Isolated") ||
      !photo_layer_clear_solo_button->property("visible").toBool()) {
    return fail("layer panel should show clear isolation while a layer is isolated");
  }
  if (!editor_preview.clearPhotoLayerSolo()) {
    return fail("layer panel should clear isolation from the header control");
  }
  QCoreApplication::processEvents();

  auto expect_capability_registry = [&](const char* count_method,
                                        const char* signature_method,
                                        const QStringList& expected_capabilities,
                                        const char* registry_name,
                                        const char* workflow_name) -> int {
    QVariant capability_count;
    QVariant capability_signature;
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            count_method,
            Q_RETURN_ARG(QVariant, capability_count)) ||
        capability_count.toInt() < expected_capabilities.size() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            signature_method,
            Q_RETURN_ARG(QVariant, capability_signature))) {
      std::cerr << "workspace should expose the " << registry_name << " capability registry\n";
      return EXIT_FAILURE;
    }
    const auto signature = capability_signature.toString();
    for (const auto& capability : expected_capabilities) {
      if (!signature.contains(capability)) {
        std::cerr << "workspace should expose every " << workflow_name
                  << " workflow capability\n";
        return EXIT_FAILURE;
      }
    }
    return EXIT_SUCCESS;
  };
  const auto edit_recipe_architecture_result =
      expect_capability_registry(
          "editRecipeArchitectureCapabilityCount",
          "editRecipeArchitectureCapabilitySignature",
          {
              QStringLiteral("unified-structured-recipe-model"),
          },
          "Edit Recipes",
          "Edit Recipes");
  if (edit_recipe_architecture_result != EXIT_SUCCESS) {
    return edit_recipe_architecture_result;
  }
  const auto color_science_capability_result =
      expect_capability_registry(
          "colorScienceCapabilityCount",
          "colorScienceCapabilitySignature",
          {
              QStringLiteral("icc-grade-color-management-layer"),
              QStringLiteral("ocio-style-configuration"),
          },
          "Color Science",
          "Color Science");
  if (color_science_capability_result != EXIT_SUCCESS) {
    return color_science_capability_result;
  }
  edit_workspace->setProperty("editTool", QStringLiteral("adjust"));
  edit_workspace->setProperty("loupeEnabled", true);
  edit_workspace->setProperty("loupeX", 96.0);
  edit_workspace->setProperty("loupeY", 72.0);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* loupe_view = edit_workspace->findChild<QObject*>(QStringLiteral("loupeView"));
  if (loupe_view == nullptr || !edit_workspace->property("loupeEnabled").toBool() ||
      edit_workspace->property("loupeX").toDouble() < 95.9 ||
      edit_workspace->property("loupeY").toDouble() < 71.9) {
    return fail("edit workspace should expose a loupe view for close inspection");
  }
  QVariant overlay_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "analysisOverlaySignature",
          Q_RETURN_ARG(QVariant, overlay_signature)) ||
      overlay_signature.toString() !=
          QStringLiteral("active:|panel:false|focus:false|peaking:false|clipping:false|false:false|gamut:false|zone:false")) {
    return fail("analysis overlays should be off by default to keep the edit canvas uncluttered");
  }
  for (auto iteration = 0; iteration < 12 && editor_preview.processing(); ++iteration) {
    QCoreApplication::processEvents();
  }
  if (editor_preview.processing()) {
    return fail("diagnostic overlay render-state proof requires an idle preview before toggling overlays");
  }
  const auto overlay_recipe_before = editor_preview.currentRecipeJson();
  const auto overlay_preview_source_before = editor_preview.displayPreviewSource();
  QVariant focus_overlay_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setAnalysisOverlay",
          Q_RETURN_ARG(QVariant, focus_overlay_result),
          Q_ARG(QVariant, QStringLiteral("focus-check"))) ||
      !focus_overlay_result.toBool()) {
    return fail("edit workspace should expose a focus-check overlay command");
  }
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* focus_check_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("focusCheckOverlay"));
  if (focus_check_overlay == nullptr ||
      !edit_workspace->property("focusCheckOverlayEnabled").toBool()) {
    return fail("edit workspace should expose a focus-check overlay");
  }
  QVariant peaking_overlay_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setAnalysisOverlay",
          Q_RETURN_ARG(QVariant, peaking_overlay_result),
          Q_ARG(QVariant, QStringLiteral("focus-peaking"))) ||
      !peaking_overlay_result.toBool()) {
    return fail("edit workspace should expose a focus peaking overlay command");
  }
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* focus_peaking_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("focusPeakingOverlay"));
  if (focus_peaking_overlay == nullptr ||
      !edit_workspace->property("focusPeakingOverlayEnabled").toBool() ||
      edit_workspace->property("focusCheckOverlayEnabled").toBool()) {
    return fail("edit workspace should expose focus peaking when source data supports it");
  }
  QVariant clipping_overlay_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setAnalysisOverlay",
          Q_RETURN_ARG(QVariant, clipping_overlay_result),
          Q_ARG(QVariant, QStringLiteral("clipping"))) ||
      !clipping_overlay_result.toBool()) {
    return fail("edit workspace should expose highlight and shadow clipping overlay commands");
  }
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* clipping_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("clippingOverlay"));
  if (clipping_overlay == nullptr || !edit_workspace->property("clippingOverlayEnabled").toBool() ||
      edit_workspace->property("focusPeakingOverlayEnabled").toBool()) {
    return fail("edit workspace should expose highlight and shadow clipping overlays");
  }
  QVariant false_color_overlay_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setAnalysisOverlay",
          Q_RETURN_ARG(QVariant, false_color_overlay_result),
          Q_ARG(QVariant, QStringLiteral("false-color"))) ||
      !false_color_overlay_result.toBool()) {
    return fail("edit workspace should expose a false-color overlay command");
  }
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* false_color_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("falseColorOverlay"));
  if (false_color_overlay == nullptr ||
      !edit_workspace->property("falseColorOverlayEnabled").toBool() ||
      edit_workspace->property("clippingOverlayEnabled").toBool()) {
    return fail("edit workspace should expose a false-color overlay");
  }
  QVariant gamut_overlay_result;
  QVariant zone_overlay_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setAnalysisOverlay",
          Q_RETURN_ARG(QVariant, gamut_overlay_result),
          Q_ARG(QVariant, QStringLiteral("gamut"))) ||
      !gamut_overlay_result.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setAnalysisOverlay",
          Q_RETURN_ARG(QVariant, zone_overlay_result),
          Q_ARG(QVariant, QStringLiteral("zone-map"))) ||
      !zone_overlay_result.toBool()) {
    return fail("edit workspace should expose gamut and zone-map analysis overlay commands");
  }
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* gamut_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("gamutOverlay"));
  auto* zone_map_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("zoneMapOverlay"));
  auto* clipping_overlay_target_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("clippingOverlayTargetLabel"));
  auto* gamut_overlay_target_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("gamutOverlayTargetLabel"));
  auto* pixel_inspector_hud = edit_workspace->findChild<QObject*>(QStringLiteral("pixelInspectorHud"));
  auto* pixel_inspector_before_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("pixelInspectorBeforeLabel"));
  auto* pixel_inspector_after_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("pixelInspectorAfterLabel"));
  auto* pixel_inspector_delta_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("pixelInspectorDeltaLabel"));
  if (gamut_overlay == nullptr || zone_map_overlay == nullptr ||
      edit_workspace->property("gamutOverlayEnabled").toBool() ||
      !edit_workspace->property("zoneMapOverlayEnabled").toBool()) {
    return fail("edit workspace should expose gamut and zone-map analysis overlays");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "analysisOverlaySignature",
          Q_RETURN_ARG(QVariant, overlay_signature)) ||
      overlay_signature.toString() !=
          QStringLiteral("active:zone-map|panel:false|focus:false|peaking:false|clipping:false|false:false|gamut:false|zone:true")) {
    return fail("analysis overlays should be mutually exclusive so diagnostic views cannot stack over the photo");
  }
  QVariant disable_overlay_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setAnalysisOverlay",
          Q_RETURN_ARG(QVariant, disable_overlay_result),
          Q_ARG(QVariant, QStringLiteral("zone-map"))) ||
      !disable_overlay_result.toBool()) {
    return fail("active diagnostic overlays should be directly dismissible");
  }
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "analysisOverlaySignature",
          Q_RETURN_ARG(QVariant, overlay_signature)) ||
      overlay_signature.toString() !=
          QStringLiteral("active:|panel:false|focus:false|peaking:false|clipping:false|false:false|gamut:false|zone:false") ||
      editor_preview.currentRecipeJson() != overlay_recipe_before ||
      editor_preview.displayPreviewSource() != overlay_preview_source_before ||
      editor_preview.processing()) {
    return fail("disabled diagnostic overlays should leave recipe state and preview pixels untouched");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setAnalysisOverlay",
          Q_RETURN_ARG(QVariant, zone_overlay_result),
          Q_ARG(QVariant, QStringLiteral("zone-map"))) ||
      !zone_overlay_result.toBool()) {
    return fail("diagnostic overlay setup should restore zone-map state for toolbar coverage");
  }
  QCoreApplication::processEvents();
  if (clipping_overlay_target_label == nullptr || gamut_overlay_target_label == nullptr ||
      !clipping_overlay_target_label->property("text").toString().contains(QStringLiteral("Preview clipping")) ||
      !gamut_overlay_target_label->property("text").toString().contains(QStringLiteral("Output gamut"))) {
    return fail("analysis overlays should label the clipping and gamut target they refer to");
  }
  if (pixel_inspector_hud == nullptr || pixel_inspector_before_label == nullptr ||
      pixel_inspector_after_label == nullptr || pixel_inspector_delta_label == nullptr) {
    return fail("edit workspace should expose a before/after pixel inspector HUD");
  }
  QVariant pixel_hover_signature;
  QVariant pixel_sampling_signature;
  QVariant pixel_crop_signature;
  QVariant pixel_sampling_begin_result;
  QVariant pixel_sampling_finish_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "pixelInspectorVisibilitySignature",
          Q_RETURN_ARG(QVariant, pixel_hover_signature),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, QStringLiteral("adjust"))) ||
      pixel_hover_signature.toString() !=
          QStringLiteral("sampling:false|active:true|tool:adjust|visible:false") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "pixelInspectorVisibilitySignature",
          Q_RETURN_ARG(QVariant, pixel_sampling_signature),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, QStringLiteral("adjust"))) ||
      pixel_sampling_signature.toString() !=
          QStringLiteral("sampling:true|active:true|tool:adjust|visible:true") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "pixelInspectorVisibilitySignature",
          Q_RETURN_ARG(QVariant, pixel_crop_signature),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, QStringLiteral("crop"))) ||
      pixel_crop_signature.toString() !=
          QStringLiteral("sampling:true|active:true|tool:crop|visible:false") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "beginPixelInspectorSampling",
          Q_RETURN_ARG(QVariant, pixel_sampling_begin_result)) ||
      !pixel_sampling_begin_result.toBool() ||
      !edit_workspace->property("pixelInspectorSamplingActive").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "finishPixelInspectorSampling",
          Q_RETURN_ARG(QVariant, pixel_sampling_finish_result)) ||
      pixel_sampling_finish_result.toBool() ||
      edit_workspace->property("pixelInspectorSamplingActive").toBool()) {
    return fail("pixel inspector should appear only during explicit sampling and clear after release or cancel");
  }
  edit_workspace->setProperty("editTool", QStringLiteral("compare"));
  edit_workspace->setProperty("compareViewMode", QStringLiteral("split"));
  edit_workspace->setProperty("compareSplitPosition", 0.35);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* before_after_split = edit_workspace->findChild<QObject*>(QStringLiteral("beforeAfterSplitView"));
  auto* focused_compare_panel =
      find_named_object(edit_workspace, QStringLiteral("beforeAfterSplitPanel"));
  if (!editor_preview.compareMode() ||
      edit_workspace->property("compareViewMode").toString() != QStringLiteral("split") ||
      edit_workspace->property("compareSplitPosition").toDouble() < 0.34 ||
      before_after_split == nullptr || focused_compare_panel == nullptr ||
      !focused_compare_panel->property("visible").toBool() ||
      edit_workspace->property("compareViewportCount").toInt() != 0) {
    return fail("split compare should dedicate the compare workspace to the before/after view");
  }
  edit_workspace->setProperty("compareViewMode", QStringLiteral("wipe"));
  edit_workspace->setProperty("compareSplitPosition", 0.65);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* before_after_wipe = edit_workspace->findChild<QObject*>(QStringLiteral("beforeAfterWipeView"));
  if (edit_workspace->property("compareViewMode").toString() != QStringLiteral("wipe") ||
      edit_workspace->property("compareSplitPosition").toDouble() < 0.64 ||
      before_after_wipe == nullptr || !focused_compare_panel->property("visible").toBool() ||
      edit_workspace->property("compareViewportCount").toInt() != 0) {
    return fail("wipe compare should keep the focused before/after view without extra columns");
  }
  edit_workspace->setProperty("compareViewMode", QStringLiteral("side-by-side"));
  edit_workspace->setProperty("syncEnabled", true);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  if (edit_workspace->property("compareViewportCount").toInt() != 2 ||
      focused_compare_panel->property("visible").toBool()) {
    return fail("side-by-side compare should expose only synchronized compare image viewports");
  }
  edit_workspace->setProperty("synchronizedZoom", 2.0);
  edit_workspace->setProperty("synchronizedPanX", 18.0);
  edit_workspace->setProperty("synchronizedPanY", -9.0);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  const auto synced_zoom = edit_workspace->property("synchronizedZoom").toDouble();
  const auto synced_pan_x = edit_workspace->property("synchronizedPanX").toDouble();
  const auto synced_pan_y = edit_workspace->property("synchronizedPanY").toDouble();
  if (synced_zoom < 1.99 || synced_zoom > 2.01 || synced_pan_x < 17.9 ||
      synced_pan_x > 18.1 || synced_pan_y > -8.9 || synced_pan_y < -9.1) {
    return fail("compare viewports should synchronize zoom and pan");
  }
  edit_workspace->setProperty("librarySourcesPanelVisible", false);
  edit_workspace->setProperty("libraryInspectorPanelVisible", false);
  edit_workspace->setProperty("editContextPanelVisible", false);
  edit_workspace->setProperty("editInspectorPanelVisible", false);
  edit_workspace->setProperty("exportPresetsPanelVisible", false);
  edit_workspace->setProperty("exportSettingsPanelVisible", false);
  QVariant panel_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "customizablePanelSignature",
          Q_RETURN_ARG(QVariant, panel_signature)) ||
      panel_signature.toString() != QStringLiteral("false|false|false|false|false|false")) {
    return fail("workspace panels should expose customizable visibility state");
  }
  if (!QMetaObject::invokeMethod(edit_workspace, "resetCustomizablePanels")) {
    return fail("workspace panels should expose a reset action");
  }
  QCoreApplication::processEvents();
  QVariant reset_panel_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "customizablePanelSignature",
          Q_RETURN_ARG(QVariant, reset_panel_signature)) ||
      reset_panel_signature.toString() != QStringLiteral("true|true|true|true|true|true")) {
    return fail("workspace panel customization reset should restore default panels");
  }
  edit_workspace->setProperty("librarySourcesPanelVisible", false);
  edit_workspace->setProperty("editContextPanelVisible", false);
  edit_workspace->setProperty("editInspectorPanelVisible", false);
  QVariant save_layout_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "saveWorkspaceLayout",
          Q_RETURN_ARG(QVariant, save_layout_result),
          Q_ARG(QVariant, QStringLiteral("Review Focus"))) ||
      !save_layout_result.toBool() || edit_workspace->property("savedWorkspaceLayoutCount").toInt() < 3) {
    return fail("workspace layout presets should save named panel arrangements");
  }
  if (!QMetaObject::invokeMethod(edit_workspace, "resetCustomizablePanels")) {
    return fail("workspace layout presets should allow reset before restore");
  }
  QVariant apply_layout_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "applyWorkspaceLayout",
          Q_RETURN_ARG(QVariant, apply_layout_result),
          Q_ARG(QVariant, QStringLiteral("Review Focus"))) ||
      !apply_layout_result.toBool()) {
    return fail("workspace layout presets should restore named panel arrangements");
  }
  QVariant restored_layout_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "customizablePanelSignature",
          Q_RETURN_ARG(QVariant, restored_layout_signature)) ||
      restored_layout_signature.toString() != QStringLiteral("false|true|false|false|true|true")) {
    return fail("workspace layout presets should restore saved panel visibility");
  }
  QVariant compact_mode_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setInterfaceMode",
          Q_RETURN_ARG(QVariant, compact_mode_result),
          Q_ARG(QVariant, QStringLiteral("compact"))) ||
      !compact_mode_result.toBool() ||
      edit_workspace->property("interfaceMode").toString() != QStringLiteral("compact") ||
      edit_workspace->property("editInspectorPanelWidth").toInt() >= 420) {
    return fail("workspace should expose compact panel density mode");
  }
  root->setProperty("width", 1180);
  root->setProperty("height", 820);
  edit_workspace->setProperty("editContextPanelVisible", true);
  edit_workspace->setProperty("editSidebarCollapsed", false);
  edit_workspace->setProperty("editInspectorPanelVisible", true);
  edit_workspace->setProperty("largeScopesMode", false);
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  edit_workspace->setProperty("diagnosticsPanelOpen", true);
  QMetaObject::invokeMethod(
      edit_workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
  process_layout_events();
  const QStringList compact_named_buttons{
      QStringLiteral("fitToViewButton"),
      QStringLiteral("fillViewButton"),
      QStringLiteral("zoomStepButton"),
      QStringLiteral("zoomOutButton"),
      QStringLiteral("zoomInButton"),
      QStringLiteral("diagnosticsOverlayButton"),
      QStringLiteral("resetViewButton"),
      QStringLiteral("referenceImageButton"),
      QStringLiteral("inspectorPanelToggleButton"),
      QStringLiteral("interfaceDensityButton"),
      QStringLiteral("fullScreenEditButton"),
      QStringLiteral("diagnosticsPanelCloseButton"),
      QStringLiteral("loupeOverlayButton"),
      QStringLiteral("focusOverlayButton"),
      QStringLiteral("peakingOverlayButton"),
      QStringLiteral("clippingOverlayButton"),
      QStringLiteral("falseColorOverlayButton"),
      QStringLiteral("gamutOverlayButton"),
      QStringLiteral("zoneMapOverlayButton"),
      QStringLiteral("adjustInspectorTabButton"),
      QStringLiteral("layersInspectorTabButton"),
      QStringLiteral("scopesInspectorTabButton"),
      QStringLiteral("detailInspectorTabButton"),
      QStringLiteral("infoInspectorTabButton"),
      QStringLiteral("editInspectorResetButton"),
      QStringLiteral("editInspectorSavePresetButton"),
  };
  const QStringList compact_named_labels{
      QStringLiteral("editAdjustmentFilterStatusLabel"),
      QStringLiteral("activeMaskToolbarStatusLabel"),
      QStringLiteral("maskStackSetLabel"),
      QStringLiteral("maskStackSetSummaryLabel"),
      QStringLiteral("cropToolHeaderLabel"),
      QStringLiteral("cropToolAspectLabel"),
      QStringLiteral("brushHudStatusLabel"),
      QStringLiteral("retouchHudStatusLabel"),
      QStringLiteral("largeScopeProofingStatusLabel"),
      QStringLiteral("pixelInspectorCoordinateLabel"),
      QStringLiteral("pixelInspectorBeforeLabel"),
      QStringLiteral("pixelInspectorAfterLabel"),
      QStringLiteral("pixelInspectorDeltaLabel"),
  };
  QString failing_compact_button;
  if (!visible_named_buttons_fit_content(edit_workspace, compact_named_buttons) ||
      !visible_mini_buttons_fit_content(edit_workspace, &failing_compact_button) ||
      !visible_named_labels_fit_content(edit_workspace, compact_named_labels)) {
    std::cerr << "Compact fit failure: "
              << failing_compact_button.toStdString() << '\n';
    return fail("compact mode should keep visible labels and button content inside controls");
  }
  edit_workspace->setProperty("diagnosticsPanelOpen", false);
  QVariant pro_mode_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setInterfaceMode",
          Q_RETURN_ARG(QVariant, pro_mode_result),
          Q_ARG(QVariant, QStringLiteral("pro"))) ||
      !pro_mode_result.toBool() ||
      edit_workspace->property("interfaceMode").toString() != QStringLiteral("pro") ||
      edit_workspace->property("editInspectorPanelWidth").toInt() < 420) {
    return fail("workspace should expose pro panel density mode");
  }
  edit_workspace->setProperty("editContextPanelVisible", true);
  edit_workspace->setProperty("editSidebarCollapsed", false);
  edit_workspace->setProperty("largeScopesMode", false);
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  edit_workspace->setProperty("compareViewMode", QStringLiteral("split"));
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activateEditTool",
          Q_ARG(QVariant, QStringLiteral("compare")))) {
    return fail("edit workspace should expose compare activation");
  }
  QCoreApplication::processEvents();
  auto* edit_context_panel = edit_workspace->findChild<QObject*>(QStringLiteral("editContextPanel"));
  auto* before_after_split_panel = find_named_object(edit_workspace, QStringLiteral("beforeAfterSplitPanel"));
  if (edit_context_panel == nullptr || before_after_split_panel == nullptr ||
      edit_workspace->property("editContextPanelActive").toBool() ||
      edit_context_panel->property("visible").toBool() ||
      !before_after_split_panel->property("visible").toBool()) {
    return fail("compare view should hide the context rail and dedicate the canvas to before/after panels");
  }
  QMetaObject::invokeMethod(
      edit_workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
  QCoreApplication::processEvents();
  if (!edit_workspace->property("editContextPanelActive").toBool() ||
      !edit_context_panel->property("visible").toBool() ||
      edit_context_panel->property("title").toString() != QStringLiteral("Library")) {
    return fail("adjust view should restore the left library rail without asset context chrome");
  }
  auto* edit_image_viewport = find_named_object(edit_workspace, QStringLiteral("editImageViewport"));
  auto* edit_zoom_toolbar = find_named_object(edit_workspace, QStringLiteral("editZoomToolbar"));
  auto* edit_action_toolbar = find_named_object(edit_workspace, QStringLiteral("editActionToolbar"));
  auto* undo_edit_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("undoEditShortcut"));
  auto* redo_edit_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("redoEditShortcut"));
  auto* diagnostics_overlay_button =
      find_named_object(edit_workspace, QStringLiteral("diagnosticsOverlayButton"));
  auto* diagnostics_overlay_panel =
      find_named_object(edit_workspace, QStringLiteral("diagnosticsOverlayPanel"));
  auto* loupe_overlay_button = find_named_object(edit_workspace, QStringLiteral("loupeOverlayButton"));
  auto* gamut_overlay_button = find_named_object(edit_workspace, QStringLiteral("gamutOverlayButton"));
  auto* preview_update_badge =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportPreviewUpdateBadge"));
  auto* preview_retry_button =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportPreviewRetryButton"));
  auto* mask_activity_status_chip =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportMaskActivityStatusChip"));
  auto* mask_activity_status_dot =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportMaskActivityStatusDot"));
  if (edit_image_viewport == nullptr || edit_zoom_toolbar == nullptr || edit_action_toolbar == nullptr ||
      undo_edit_shortcut == nullptr || redo_edit_shortcut == nullptr ||
      diagnostics_overlay_button == nullptr || diagnostics_overlay_panel == nullptr ||
      loupe_overlay_button == nullptr || gamut_overlay_button == nullptr ||
      preview_update_badge == nullptr || preview_retry_button == nullptr ||
      mask_activity_status_chip == nullptr || mask_activity_status_dot == nullptr) {
    return fail("edit canvas should expose responsive viewport toolbar controls");
  }
  QVariant preview_update_signature;
  QVariant preview_failure_signature;
  QVariant preview_stage_failure_signature;
  QVariant preview_unavailable_signature;
  QVariant preview_update_chrome_signature;
  QVariant preview_failure_chrome_signature;
  QVariant preview_layer_failure_row_signature;
  QVariant preview_inactive_layer_failure_row_signature;
  QVariant preview_mask_failure_row_signature;
  QVariant mask_activity_analyzing_signature;
  QVariant mask_activity_rasterizing_signature;
  QVariant mask_activity_inpainting_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "previewRenderPresentationSignature",
          Q_RETURN_ARG(QVariant, preview_update_signature),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, QStringLiteral("Rendering preview")),
          Q_ARG(QVariant, QString()),
          Q_ARG(QVariant, QString())) ||
      preview_update_signature.toString() !=
          QStringLiteral("updating|blocking:false|stale:true|retry:false") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "previewRenderPresentationSignature",
          Q_RETURN_ARG(QVariant, preview_failure_signature),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, QStringLiteral("Preview render failed")),
          Q_ARG(QVariant, QString()),
          Q_ARG(QVariant, QString())) ||
      preview_failure_signature.toString() !=
          QStringLiteral("failed|blocking:false|stale:false|retry:true") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "previewRenderPresentationSignature",
          Q_RETURN_ARG(QVariant, preview_stage_failure_signature),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable.")),
          Q_ARG(QVariant, QStringLiteral("source_decode")),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable."))) ||
      preview_stage_failure_signature.toString() !=
          QStringLiteral("failed|blocking:false|stale:false|retry:true|stage:source_decode|detail:true") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "previewRenderPresentationSignature",
          Q_RETURN_ARG(QVariant, preview_unavailable_signature),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable.")),
          Q_ARG(QVariant, QString()),
          Q_ARG(QVariant, QString())) ||
      preview_unavailable_signature.toString() !=
          QStringLiteral("failed|blocking:false|stale:false|retry:true") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "previewRenderChromeSignature",
          Q_RETURN_ARG(QVariant, preview_update_chrome_signature),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, QStringLiteral("Rendering preview")),
          Q_ARG(QVariant, QString()),
          Q_ARG(QVariant, QString())) ||
      preview_update_chrome_signature.toString() !=
          QStringLiteral(
              "updating|blocking:false|stale:true|retry:false|chrome:badge|center:false|retryButton:false") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "previewRenderChromeSignature",
          Q_RETURN_ARG(QVariant, preview_failure_chrome_signature),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable.")),
          Q_ARG(QVariant, QString()),
          Q_ARG(QVariant, QString())) ||
      preview_failure_chrome_signature.toString() !=
          QStringLiteral(
              "failed|blocking:false|stale:false|retry:true|chrome:badge|center:false|retryButton:true") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "previewFailureRowPresentationSignature",
          Q_RETURN_ARG(QVariant, preview_layer_failure_row_signature),
          Q_ARG(QVariant, QStringLiteral("layer")),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable.")),
          Q_ARG(QVariant, QStringLiteral("source_decode")),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable."))) ||
      preview_layer_failure_row_signature.toString() !=
          QStringLiteral("row:layer|failed:true|stage:source_decode|label:true") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "previewFailureRowPresentationSignature",
          Q_RETURN_ARG(QVariant, preview_inactive_layer_failure_row_signature),
          Q_ARG(QVariant, QStringLiteral("layer")),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable.")),
          Q_ARG(QVariant, QStringLiteral("source_decode")),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable."))) ||
      preview_inactive_layer_failure_row_signature.toString() !=
          QStringLiteral("row:layer|failed:false|stage:|label:false") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "previewFailureRowPresentationSignature",
          Q_RETURN_ARG(QVariant, preview_mask_failure_row_signature),
          Q_ARG(QVariant, QStringLiteral("mask")),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable.")),
          Q_ARG(QVariant, QStringLiteral("source_decode")),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable."))) ||
      preview_mask_failure_row_signature.toString() !=
          QStringLiteral("row:mask|failed:true|stage:source_decode|label:true") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskActivityPresentationSignature",
          Q_RETURN_ARG(QVariant, mask_activity_analyzing_signature),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, QStringLiteral("Smart selection analyzing target")),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, QStringLiteral("masks"))) ||
      mask_activity_analyzing_signature.toString() !=
          QStringLiteral("analyzing|visible:true|blocking:false|chip:persistent") ||
      mask_activity_status_chip->property("activityAnimationContract").toString() !=
          QStringLiteral("nonblocking|persistent|animated-dot:v1") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskActivityPresentationSignature",
          Q_RETURN_ARG(QVariant, mask_activity_rasterizing_signature),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, QStringLiteral("Rendering mask rasterization")),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, QStringLiteral("brush"))) ||
      mask_activity_rasterizing_signature.toString() !=
          QStringLiteral("rasterizing|visible:true|blocking:false|chip:persistent") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskActivityPresentationSignature",
          Q_RETURN_ARG(QVariant, mask_activity_inpainting_signature),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, QStringLiteral("LaMa Model Inpaint patch baking")),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, QStringLiteral("retouch"))) ||
      mask_activity_inpainting_signature.toString() !=
          QStringLiteral("inpainting|visible:true|blocking:false|chip:persistent")) {
    return fail("preview render status should distinguish stale updating from retryable failure without blocking the canvas");
  }
  const QStringList zoom_toolbar_items{
      QStringLiteral("fitToViewButton"),
      QStringLiteral("fillViewButton"),
      QStringLiteral("zoomStepButton"),
      QStringLiteral("zoomOutButton"),
      QStringLiteral("zoomInButton"),
      QStringLiteral("diagnosticsOverlayButton"),
      QStringLiteral("resetViewButton"),
  };
  const QStringList action_toolbar_items{
      QStringLiteral("referenceImageButton"),
      QStringLiteral("clearReferenceImageButton"),
      QStringLiteral("inspectorPanelToggleButton"),
      QStringLiteral("interfaceDensityButton"),
      QStringLiteral("fullScreenEditButton"),
  };
  const QStringList diagnostics_panel_items{
      QStringLiteral("diagnosticsPanelCloseButton"),
      QStringLiteral("loupeOverlayButton"),
      QStringLiteral("focusOverlayButton"),
      QStringLiteral("peakingOverlayButton"),
      QStringLiteral("clippingOverlayButton"),
      QStringLiteral("falseColorOverlayButton"),
      QStringLiteral("gamutOverlayButton"),
      QStringLiteral("zoneMapOverlayButton"),
  };
  const QList<int> supported_toolbar_widths{1024, 1180, 1320, 1600};
  for (const auto supported_width : supported_toolbar_widths) {
    root->setProperty("width", supported_width);
    root->setProperty("height", 820);
    QMetaObject::invokeMethod(
        edit_workspace,
        "applyResponsivePanelBehavior",
        Q_ARG(QVariant, supported_width));
    QMetaObject::invokeMethod(
        edit_workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
    edit_workspace->setProperty("diagnosticsPanelOpen", false);
    process_layout_events();

    edit_image_viewport = find_named_object(edit_workspace, QStringLiteral("editImageViewport"));
    edit_zoom_toolbar = find_named_object(edit_workspace, QStringLiteral("editZoomToolbar"));
    edit_action_toolbar = find_named_object(edit_workspace, QStringLiteral("editActionToolbar"));
    diagnostics_overlay_button =
        find_named_object(edit_workspace, QStringLiteral("diagnosticsOverlayButton"));
    diagnostics_overlay_panel =
        find_named_object(edit_workspace, QStringLiteral("diagnosticsOverlayPanel"));
    loupe_overlay_button = find_named_object(edit_workspace, QStringLiteral("loupeOverlayButton"));
    gamut_overlay_button = find_named_object(edit_workspace, QStringLiteral("gamutOverlayButton"));
    auto* viewport_item = qobject_cast<QQuickItem*>(edit_image_viewport);
    auto* zoom_toolbar_item = qobject_cast<QQuickItem*>(edit_zoom_toolbar);
    auto* action_toolbar_item = qobject_cast<QQuickItem*>(edit_action_toolbar);
    if (!item_visible(viewport_item) || !item_visible(zoom_toolbar_item) ||
        !item_visible(action_toolbar_item)) {
      return fail("edit canvas should keep generic toolbar items visible at supported widths");
    }

    const auto viewport_width = viewport_item->width();
    const auto toolbar_compact_width =
        edit_image_viewport->property("toolbarCompactWidth").toDouble();
    const auto toolbar_compact = viewport_width < toolbar_compact_width;
    const auto zoom_right = edit_zoom_toolbar->property("x").toDouble() +
                            edit_zoom_toolbar->property("width").toDouble();
    const auto action_left = edit_action_toolbar->property("x").toDouble();
    const auto zoom_bottom = edit_zoom_toolbar->property("y").toDouble() +
                             edit_zoom_toolbar->property("height").toDouble();
    const auto action_top = edit_action_toolbar->property("y").toDouble();
    const auto toolbar_stacked = edit_image_viewport->property("toolbarStacked").toBool();
    const auto viewport_rect = scene_rect(viewport_item);
    if (edit_image_viewport->property("toolbarCompact").toBool() != toolbar_compact ||
        edit_zoom_toolbar->property("toolGroupContract").toString() !=
            QStringLiteral("view-controls|zoom-fit-diagnostics|no-history-buttons:v1") ||
        edit_action_toolbar->property("toolGroupContract").toString() !=
            QStringLiteral("canvas-actions|view-separated|no-history-buttons:v1") ||
        find_named_object(edit_workspace, QStringLiteral("undoEditButton")) != nullptr ||
        find_named_object(edit_workspace, QStringLiteral("redoEditButton")) != nullptr ||
        diagnostics_overlay_button->property("labelVisible").toBool() ||
        diagnostics_overlay_button->property("text").toString() != QStringLiteral("Zone") ||
        diagnostics_overlay_panel->property("visible").toBool() ||
        loupe_overlay_button->property("visible").toBool() ||
        gamut_overlay_button->property("visible").toBool() ||
        gamut_overlay_button->property("tooltip").toString().isEmpty() ||
        !contains_with_tolerance(viewport_rect, scene_rect(zoom_toolbar_item)) ||
        !contains_with_tolerance(viewport_rect, scene_rect(action_toolbar_item)) ||
        !visible_named_items_fit_parent(
            edit_workspace,
            QStringLiteral("editZoomToolbar"),
            zoom_toolbar_items) ||
        !visible_named_items_fit_parent(
            edit_workspace,
            QStringLiteral("editActionToolbar"),
            action_toolbar_items) ||
        !visible_named_items_do_not_overlap(edit_workspace, zoom_toolbar_items) ||
        !visible_named_items_do_not_overlap(edit_workspace, action_toolbar_items) ||
        (!toolbar_stacked && zoom_right + 8.0 > action_left) ||
        (toolbar_stacked && zoom_bottom + 6.0 > action_top)) {
      return fail("edit canvas toolbars should compact predictably and avoid overlap");
    }

    edit_workspace->setProperty("diagnosticsPanelOpen", true);
    process_layout_events();
    diagnostics_overlay_panel =
        find_named_object(edit_workspace, QStringLiteral("diagnosticsOverlayPanel"));
    auto* diagnostics_overlay_panel_item = qobject_cast<QQuickItem*>(diagnostics_overlay_panel);
    const auto diagnostics_panel_visible = item_visible(diagnostics_overlay_panel_item);
    const auto diagnostics_panel_inside_viewport =
        diagnostics_panel_visible &&
        contains_with_tolerance(viewport_rect, scene_rect(diagnostics_overlay_panel_item));
    const auto diagnostics_items_fit = visible_named_items_fit_parent(
        edit_workspace,
        QStringLiteral("diagnosticsOverlayPanel"),
        diagnostics_panel_items);
    const auto diagnostics_items_clear =
        visible_named_items_do_not_overlap(edit_workspace, diagnostics_panel_items);
    if (!diagnostics_panel_visible || !diagnostics_panel_inside_viewport ||
        !diagnostics_items_fit || !diagnostics_items_clear) {
      std::cerr << "Diagnostics toolbar width=" << supported_width
                << " panelVisible=" << diagnostics_panel_visible
                << " panelInside=" << diagnostics_panel_inside_viewport
                << " itemsFit=" << diagnostics_items_fit
                << " itemsClear=" << diagnostics_items_clear << '\n';
      if (diagnostics_panel_visible) {
        const auto rect = scene_rect(diagnostics_overlay_panel_item);
        std::cerr << "diagnosticsOverlayPanel rect=" << rect.x() << "," << rect.y()
                  << " " << rect.width() << "x" << rect.height() << '\n';
      }
      log_visible_named_item_geometry(edit_workspace, diagnostics_panel_items);
      return fail("diagnostic overlay toolbar should fit and avoid button overlap at supported widths");
    }
    edit_workspace->setProperty("diagnosticsPanelOpen", false);
  }
  root->setProperty("width", 1600);
  root->setProperty("height", 820);
  QMetaObject::invokeMethod(
      edit_workspace,
      "applyResponsivePanelBehavior",
      Q_ARG(QVariant, 1600));
  QMetaObject::invokeMethod(
      edit_workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
  process_layout_events();
  edit_workspace->setProperty("diagnosticsPanelOpen", true);
  QCoreApplication::processEvents();
  if (!diagnostics_overlay_panel->property("visible").toBool() ||
      !loupe_overlay_button->property("visible").toBool() ||
      !gamut_overlay_button->property("visible").toBool()) {
    return fail("diagnostic overlay choices should stay collapsed behind one toolbar control until requested");
  }
  edit_workspace->setProperty("diagnosticsPanelOpen", false);
  QCoreApplication::processEvents();
  QVariant full_screen_result;
  auto* edit_filmstrip = find_named_object(edit_workspace, QStringLiteral("editFilmstrip"));
  auto* edit_inspector_panel_for_focus =
      find_named_object(edit_workspace, QStringLiteral("editInspectorPanel"));
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setFullScreenEditing",
          Q_RETURN_ARG(QVariant, full_screen_result),
          Q_ARG(QVariant, true)) ||
      !full_screen_result.toBool() ||
      !edit_workspace->property("fullScreenEditingEnabled").toBool() ||
      edit_workspace->property("editingChromeVisible").toBool()) {
    return fail("edit workspace should expose a full-screen editing mode");
  }
  QVariant exit_full_screen_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setFullScreenEditing",
          Q_RETURN_ARG(QVariant, exit_full_screen_result),
          Q_ARG(QVariant, false)) ||
      exit_full_screen_result.toBool() ||
      edit_workspace->property("fullScreenEditingEnabled").toBool() ||
      !edit_workspace->property("editingChromeVisible").toBool()) {
    return fail("edit workspace should restore chrome after full-screen editing");
  }
  QMetaObject::invokeMethod(edit_workspace, "resetCustomizablePanels");
  QCoreApplication::processEvents();
  QVariant focus_edit_result;
  const auto recipe_before_focus_edit = editor_preview.currentRecipeJson();
  if (edit_filmstrip == nullptr || edit_context_panel == nullptr ||
      edit_inspector_panel_for_focus == nullptr ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setFocusEditLayout",
          Q_RETURN_ARG(QVariant, focus_edit_result),
          Q_ARG(QVariant, true)) ||
      !focus_edit_result.toBool() ||
      !edit_workspace->property("focusEditLayoutEnabled").toBool() ||
      edit_workspace->property("fullScreenEditingEnabled").toBool() ||
      edit_workspace->property("editingChromeVisible").toBool() ||
      edit_context_panel->property("visible").toBool() ||
      edit_inspector_panel_for_focus->property("visible").toBool() ||
      edit_filmstrip->property("visible").toBool() ||
      editor_preview.currentRecipeJson() != recipe_before_focus_edit) {
    return fail("Focus Edit layout should hide context, inspector, and filmstrip chrome without using fullscreen state");
  }
  QVariant exit_focus_edit_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setFocusEditLayout",
          Q_RETURN_ARG(QVariant, exit_focus_edit_result),
          Q_ARG(QVariant, false)) ||
      exit_focus_edit_result.toBool() ||
      edit_workspace->property("focusEditLayoutEnabled").toBool() ||
      !edit_workspace->property("editingChromeVisible").toBool() ||
      !edit_context_panel->property("visible").toBool() ||
      !edit_inspector_panel_for_focus->property("visible").toBool() ||
      !edit_filmstrip->property("visible").toBool() ||
      editor_preview.currentRecipeJson() != recipe_before_focus_edit) {
    return fail("Focus Edit layout should restore normal edit chrome and the left library rail when exited");
  }
  QVariant narrow_layout_result;
  QVariant narrow_layout_signature;
  QVariant narrow_density_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "applyResponsivePanelBehavior",
          Q_RETURN_ARG(QVariant, narrow_layout_result),
          Q_ARG(QVariant, 980)) ||
      !narrow_layout_result.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "responsivePanelSignature",
          Q_RETURN_ARG(QVariant, narrow_layout_signature)) ||
      narrow_layout_signature.toString() !=
          QStringLiteral("compact|true|false|false|true|false|false|false|false") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "responsivePanelDensitySignature",
          Q_RETURN_ARG(QVariant, narrow_density_signature),
          Q_ARG(QVariant, 980)) ||
      narrow_density_signature.toString() !=
          QStringLiteral(
              "w:980|mode:small-collapsed|interface:compact|small:true|active:true|library:218/292|edit:210/356|export:258/294|visible:false,false,true,false,false,false,false")) {
    return fail("workspace should collapse panels for small displays");
  }
  QVariant breakpoint_density_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "responsivePanelDensitySignature",
          Q_RETURN_ARG(QVariant, breakpoint_density_signature),
          Q_ARG(QVariant, 1119)) ||
      breakpoint_density_signature.toString() !=
          QStringLiteral(
              "w:1119|mode:small-collapsed|interface:compact|small:true|active:true|library:218/292|edit:210/356|export:258/294|visible:false,false,true,false,false,false,false")) {
    return fail("workspace should keep the small-display signature deterministic below the breakpoint");
  }
  QVariant wide_layout_result;
  QVariant wide_layout_signature;
  QVariant wide_density_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "applyResponsivePanelBehavior",
          Q_RETURN_ARG(QVariant, wide_layout_result),
          Q_ARG(QVariant, 1440)) ||
      !wide_layout_result.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "responsivePanelSignature",
          Q_RETURN_ARG(QVariant, wide_layout_signature)) ||
      wide_layout_signature.toString() !=
          QStringLiteral("pro|false|true|true|false|true|true|true|true") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "responsivePanelDensitySignature",
          Q_RETURN_ARG(QVariant, wide_density_signature),
          Q_ARG(QVariant, 1440)) ||
      wide_density_signature.toString() !=
          QStringLiteral(
              "w:1440|mode:pro|interface:pro|small:false|active:false|library:262/340|edit:246/420|export:306/342|visible:true,true,false,true,true,true,true")) {
    return fail("workspace should restore panels after leaving small display mode");
  }
  QVariant compact_width_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setInterfaceMode",
          Q_RETURN_ARG(QVariant, compact_mode_result),
          Q_ARG(QVariant, QStringLiteral("compact"))) ||
      !compact_mode_result.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "responsivePanelDensitySignature",
          Q_RETURN_ARG(QVariant, compact_width_signature),
          Q_ARG(QVariant, 1180)) ||
      compact_width_signature.toString() !=
          QStringLiteral(
              "w:1180|mode:compact|interface:compact|small:false|active:false|library:218/292|edit:210/356|export:258/294|visible:true,true,false,true,true,true,true")) {
    return fail("workspace compact density should have deterministic panel widths above the small-display breakpoint");
  }
  QMetaObject::invokeMethod(
      edit_workspace,
      "setInterfaceMode",
      Q_RETURN_ARG(QVariant, pro_mode_result),
      Q_ARG(QVariant, QStringLiteral("pro")));
  catalog_assets.replaceAssetsForTesting({});
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  QVariant edit_empty_title;
  QVariant library_empty_title_from_edit_workspace;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "editEmptyStateTitle",
          Q_RETURN_ARG(QVariant, edit_empty_title)) ||
      edit_empty_title.toString() != QStringLiteral("No Photo Selected") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "libraryEmptyStateTitle",
          Q_RETURN_ARG(QVariant, library_empty_title_from_edit_workspace)) ||
      library_empty_title_from_edit_workspace.toString().trimmed().isEmpty()) {
    return fail("workspace should expose clear empty and error state messages");
  }
  QVariant library_capability_count;
  QVariant library_capability_signature;
  const QStringList expected_library_capabilities = {
      QStringLiteral("albums"),
      QStringLiteral("collections"),
      QStringLiteral("smart-albums"),
      QStringLiteral("stacks"),
      QStringLiteral("versions"),
      QStringLiteral("virtual-copies"),
      QStringLiteral("duplicate-detection"),
      QStringLiteral("near-duplicate-clustering"),
      QStringLiteral("offline-media"),
      QStringLiteral("relink"),
      QStringLiteral("sidecars"),
      QStringLiteral("card-ingest"),
      QStringLiteral("checksum"),
      QStringLiteral("backup-copy"),
      QStringLiteral("rename-templates"),
      QStringLiteral("folder-templates"),
      QStringLiteral("metadata-preset-import"),
      QStringLiteral("develop-preset-import"),
      QStringLiteral("duplicate-avoidance-import"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "libraryCapabilityCount",
          Q_RETURN_ARG(QVariant, library_capability_count)) ||
      library_capability_count.toInt() < expected_library_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "libraryCapabilitySignature",
          Q_RETURN_ARG(QVariant, library_capability_signature))) {
    return fail("workspace should expose the Library and Catalog capability registry");
  }
  for (const auto& capability : expected_library_capabilities) {
    if (!library_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every Library and Catalog workflow capability");
    }
  }
  QVariant metadata_capability_count;
  QVariant metadata_capability_signature;
  const QStringList expected_metadata_capabilities = {
      QStringLiteral("exif-iptc-xmp"),
      QStringLiteral("captions"),
      QStringLiteral("keywords"),
      QStringLiteral("people-tagging"),
      QStringLiteral("gps-map"),
      QStringLiteral("copyright-creator"),
      QStringLiteral("metadata-templates"),
      QStringLiteral("metadata-sync"),
      QStringLiteral("metadata-conflicts"),
      QStringLiteral("sidecar-round-trip"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "metadataCapabilityCount",
          Q_RETURN_ARG(QVariant, metadata_capability_count)) ||
      metadata_capability_count.toInt() < expected_metadata_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "metadataCapabilitySignature",
          Q_RETURN_ARG(QVariant, metadata_capability_signature))) {
    return fail("workspace should expose the Metadata capability registry");
  }
  for (const auto& capability : expected_metadata_capabilities) {
    if (!metadata_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every Metadata workflow capability");
    }
  }
  QVariant culling_search_capability_count;
  QVariant culling_search_capability_signature;
  const QStringList expected_culling_search_capabilities = {
      QStringLiteral("embedded-jpeg-previews"),
      QStringLiteral("reject-flag-rating-label-hotkeys"),
      QStringLiteral("auto-advance"),
      QStringLiteral("burst-grouping"),
      QStringLiteral("pick-groups"),
      QStringLiteral("fast-compare-candidates"),
      QStringLiteral("focus-check-zoom"),
      QStringLiteral("rich-filtering"),
      QStringLiteral("saved-searches"),
      QStringLiteral("smart-filters"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "cullingSearchCapabilityCount",
          Q_RETURN_ARG(QVariant, culling_search_capability_count)) ||
      culling_search_capability_count.toInt() < expected_culling_search_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "cullingSearchCapabilitySignature",
          Q_RETURN_ARG(QVariant, culling_search_capability_signature))) {
    return fail("workspace should expose the Culling and Search capability registry");
  }
  for (const auto& capability : expected_culling_search_capabilities) {
    if (!culling_search_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every Culling and Search workflow capability");
    }
  }
  QVariant batch_editing_capability_count;
  QVariant batch_editing_capability_signature;
  const QStringList expected_batch_editing_capabilities = {
      QStringLiteral("selective-sync"),
      QStringLiteral("copy-paste-group-selection"),
      QStringLiteral("paste-conflict-preview"),
      QStringLiteral("match-exposure"),
      QStringLiteral("match-white-balance"),
      QStringLiteral("batch-crop"),
      QStringLiteral("batch-mask-ai"),
      QStringLiteral("batch-retouch-safe"),
      QStringLiteral("batch-metadata-templates"),
      QStringLiteral("batch-export-validation"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "batchEditingCapabilityCount",
          Q_RETURN_ARG(QVariant, batch_editing_capability_count)) ||
      batch_editing_capability_count.toInt() < expected_batch_editing_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "batchEditingCapabilitySignature",
          Q_RETURN_ARG(QVariant, batch_editing_capability_signature))) {
    return fail("workspace should expose the Batch Editing capability registry");
  }
  for (const auto& capability : expected_batch_editing_capabilities) {
    if (!batch_editing_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every Batch Editing workflow capability");
    }
  }
  QVariant export_delivery_capability_count;
  QVariant export_delivery_capability_signature;
  const QStringList expected_export_delivery_capabilities = {
      QStringLiteral("custom-export-presets"),
      QStringLiteral("export-formats"),
      QStringLiteral("bit-depth-options"),
      QStringLiteral("color-profile-selection"),
      QStringLiteral("profile-embedding"),
      QStringLiteral("resize-modes"),
      QStringLiteral("output-sharpening"),
      QStringLiteral("watermarking"),
      QStringLiteral("metadata-export-controls"),
      QStringLiteral("filename-templates"),
      QStringLiteral("subfolder-templates"),
      QStringLiteral("collision-handling"),
      QStringLiteral("export-validation-warnings"),
      QStringLiteral("export-queue-controls"),
      QStringLiteral("export-manifests"),
      QStringLiteral("print-workflow"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "exportDeliveryCapabilityCount",
          Q_RETURN_ARG(QVariant, export_delivery_capability_count)) ||
      export_delivery_capability_count.toInt() < expected_export_delivery_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "exportDeliveryCapabilitySignature",
          Q_RETURN_ARG(QVariant, export_delivery_capability_signature))) {
    return fail("workspace should expose the Export and Delivery capability registry");
  }
  for (const auto& capability : expected_export_delivery_capabilities) {
    if (!export_delivery_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every Export and Delivery workflow capability");
    }
  }
  QVariant performance_capability_count;
  QVariant performance_capability_signature;
  const QStringList expected_performance_capabilities = {
      QStringLiteral("tiled-rendering"),
      QStringLiteral("mip-pyramid-previews"),
      QStringLiteral("background-cache-warming"),
      QStringLiteral("viewport-only-rendering"),
      QStringLiteral("dirty-tile-rendering"),
      QStringLiteral("progressive-refinement"),
      QStringLiteral("gpu-pipeline-coverage"),
      QStringLiteral("cross-platform-gpu-abstraction"),
      QStringLiteral("windows-gpu-parity"),
      QStringLiteral("cpu-gpu-memory-budgets"),
      QStringLiteral("disk-cache-eviction"),
      QStringLiteral("gpu-texture-pool"),
      QStringLiteral("low-memory-recovery"),
      QStringLiteral("huge-file-panorama"),
      QStringLiteral("slider-latency-targets"),
      QStringLiteral("brush-latency-targets"),
      QStringLiteral("import-throughput-targets"),
      QStringLiteral("preview-render-targets"),
      QStringLiteral("export-throughput-targets"),
      QStringLiteral("benchmark-dashboards"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "performanceCapabilityCount",
          Q_RETURN_ARG(QVariant, performance_capability_count)) ||
      performance_capability_count.toInt() < expected_performance_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "performanceCapabilitySignature",
          Q_RETURN_ARG(QVariant, performance_capability_signature))) {
    return fail("workspace should expose the Performance capability registry");
  }
  for (const auto& capability : expected_performance_capabilities) {
    if (!performance_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every Performance workflow capability");
    }
  }
  QVariant ai_model_runtime_capability_count;
  QVariant ai_model_runtime_capability_signature;
  const QStringList expected_ai_model_runtime_capabilities = {
      QStringLiteral("model-download-install-ui"),
      QStringLiteral("model-versioning"),
      QStringLiteral("acceleration-detection"),
      QStringLiteral("model-fallback-modes"),
      QStringLiteral("privacy-offline-indicators"),
      QStringLiteral("model-error-states"),
      QStringLiteral("model-device-selection"),
      QStringLiteral("quantized-model-options"),
      QStringLiteral("batch-inference"),
      QStringLiteral("preview-full-refinement"),
      QStringLiteral("model-warmup"),
      QStringLiteral("model-cache-management"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "aiModelRuntimeCapabilityCount",
          Q_RETURN_ARG(QVariant, ai_model_runtime_capability_count)) ||
      ai_model_runtime_capability_count.toInt() <
          expected_ai_model_runtime_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "aiModelRuntimeCapabilitySignature",
          Q_RETURN_ARG(QVariant, ai_model_runtime_capability_signature))) {
    return fail("workspace should expose the AI and Model Runtime capability registry");
  }
  for (const auto& capability : expected_ai_model_runtime_capabilities) {
    if (!ai_model_runtime_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every AI and Model Runtime workflow capability");
    }
  }
  const auto remaining_capability_result =
      expect_capability_registry(
          "reliabilityRecoveryCapabilityCount",
          "reliabilityRecoveryCapabilitySignature",
          {
              QStringLiteral("undo-redo-hardening"),
              QStringLiteral("visible-history-panel"),
              QStringLiteral("workspace-autosave"),
              QStringLiteral("crash-safe-recipe-writes"),
              QStringLiteral("job-recovery"),
              QStringLiteral("catalog-repair-tools"),
              QStringLiteral("cache-repair-tools"),
              QStringLiteral("relink-repair-tools"),
              QStringLiteral("project-package-collect-originals"),
              QStringLiteral("sidecar-backup-restore"),
          },
          "Reliability and Recovery",
          "Reliability and Recovery");
  if (remaining_capability_result != EXIT_SUCCESS) {
    return remaining_capability_result;
  }
  const auto collaboration_capability_result =
      expect_capability_registry(
          "collaborationHandoffCapabilityCount",
          "collaborationHandoffCapabilitySignature",
          {
              QStringLiteral("project-packaging"),
              QStringLiteral("collect-originals"),
              QStringLiteral("sidecar-recipes"),
              QStringLiteral("edit-history-export"),
              QStringLiteral("comments-annotations"),
              QStringLiteral("client-proof-galleries"),
              QStringLiteral("external-editor-roundtrip"),
              QStringLiteral("plugin-extension-api"),
              QStringLiteral("scripting-automation-hooks"),
              QStringLiteral("custom-export-actions"),
              QStringLiteral("custom-model-plugins"),
          },
          "Collaboration and Handoff",
          "Collaboration and Handoff");
  if (collaboration_capability_result != EXIT_SUCCESS) {
    return collaboration_capability_result;
  }
  const auto qa_capability_result =
      expect_capability_registry(
          "qaTestingCapabilityCount",
          "qaTestingCapabilitySignature",
          {
              QStringLiteral("golden-image-corpus"),
              QStringLiteral("raw-corpus"),
              QStringLiteral("color-chart-tests"),
              QStringLiteral("perceptual-delta-tests"),
              QStringLiteral("recipe-parser-fuzzing"),
              QStringLiteral("huge-file-stress-tests"),
              QStringLiteral("masks-layers-stress-tests"),
              QStringLiteral("catalog-import-stress-tests"),
              QStringLiteral("export-queue-stress-tests"),
              QStringLiteral("render-performance-benchmarks"),
              QStringLiteral("ui-smoke-tests-core-workflows"),
              QStringLiteral("model-runtime-tests-matrix"),
          },
          "QA and Testing",
          "QA and Testing");
  if (qa_capability_result != EXIT_SUCCESS) {
    return qa_capability_result;
  }
  const auto observability_capability_result =
      expect_capability_registry(
          "observabilityCapabilityCount",
          "observabilityCapabilitySignature",
          {
              QStringLiteral("render-timings"),
              QStringLiteral("gpu-fallback-reasons"),
              QStringLiteral("failed-exports"),
              QStringLiteral("failed-imports"),
              QStringLiteral("model-failures"),
              QStringLiteral("catalog-db-health"),
              QStringLiteral("cache-hit-rates"),
              QStringLiteral("memory-pressure"),
              QStringLiteral("privacy-safe-diagnostics-bundles"),
              QStringLiteral("crash-reporting"),
          },
          "Observability",
          "Observability");
  if (observability_capability_result != EXIT_SUCCESS) {
    return observability_capability_result;
  }
  const auto packaging_capability_result =
      expect_capability_registry(
          "packagingProductizationCapabilityCount",
          "packagingProductizationCapabilitySignature",
          {
              QStringLiteral("signed-installers"),
              QStringLiteral("macos-notarization"),
              QStringLiteral("update-channels"),
              QStringLiteral("migration-tests"),
              QStringLiteral("first-run-model-profile-setup"),
              QStringLiteral("sample-catalog"),
              QStringLiteral("reset-repair-tools"),
              QStringLiteral("licensing-entitlement-groundwork"),
              QStringLiteral("release-notes-workflow"),
          },
          "Packaging and Productization",
          "Packaging and Productization");
  if (packaging_capability_result != EXIT_SUCCESS) {
    return packaging_capability_result;
  }
  const auto accessibility_capability_result =
      expect_capability_registry(
          "accessibilityLocalizationCapabilityCount",
          "accessibilityLocalizationCapabilitySignature",
          {
              QStringLiteral("keyboard-only-editing"),
              QStringLiteral("screen-reader-labels"),
              QStringLiteral("scalable-ui"),
              QStringLiteral("high-contrast-mode"),
              QStringLiteral("reduced-motion-mode"),
              QStringLiteral("color-blind-safe-overlays"),
              QStringLiteral("visible-string-localization"),
              QStringLiteral("localized-metadata-date-number-formatting"),
          },
          "Accessibility and Localization",
          "Accessibility and Localization");
  if (accessibility_capability_result != EXIT_SUCCESS) {
    return accessibility_capability_result;
  }
  const auto documentation_capability_result =
      expect_capability_registry(
          "documentationHelpCapabilityCount",
          "documentationHelpCapabilitySignature",
          {
              QStringLiteral("searchable-command-help"),
              QStringLiteral("in-app-tool-help"),
              QStringLiteral("troubleshooting-diagnostics"),
              QStringLiteral("preset-locations-docs"),
              QStringLiteral("model-locations-docs"),
              QStringLiteral("profile-locations-docs"),
              QStringLiteral("sidecar-behavior-docs"),
              QStringLiteral("catalog-backup-recovery-docs"),
              QStringLiteral("export-color-management-docs"),
          },
          "Documentation and Help",
          "Documentation and Help");
  if (documentation_capability_result != EXIT_SUCCESS) {
    return documentation_capability_result;
  }
  const auto acceptance_criteria_result =
      expect_capability_registry(
          "acceptanceCriteriaCount",
          "acceptanceCriteriaSignature",
          {
              QStringLiteral("pro-shoot-end-to-end"),
              QStringLiteral("raw-color-quality"),
              QStringLiteral("nondestructive-recoverable-portable"),
              QStringLiteral("fast-precise-stack-masking"),
              QStringLiteral("credible-inpaint-retouch-undo"),
              QStringLiteral("layer-compositing-workflows"),
              QStringLiteral("responsive-large-workloads"),
              QStringLiteral("export-fidelity"),
              QStringLiteral("clear-failure-recovery"),
              QStringLiteral("professional-accessible-ui"),
          },
          "Acceptance Criteria",
          "Acceptance Criteria");
  if (acceptance_criteria_result != EXIT_SUCCESS) {
    return acceptance_criteria_result;
  }
  QVariant all_capability_count;
  QVariant all_capability_signature;
  QVariant capability_registry_gate_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "allCapabilityCount",
          Q_RETURN_ARG(QVariant, all_capability_count)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "allCapabilitySignature",
          Q_RETURN_ARG(QVariant, all_capability_signature)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "capabilityRegistryGateSignature",
          Q_RETURN_ARG(QVariant, capability_registry_gate_signature)) ||
      all_capability_count.toInt() < 179 ||
      !capability_registry_gate_signature.toString().startsWith(QStringLiteral("total:")) ||
      !capability_registry_gate_signature.toString().contains(QStringLiteral("|duplicates:")) ||
      !capability_registry_gate_signature.toString().endsWith(QStringLiteral("|duplicates:")) ||
      !all_capability_signature.toString().contains(QStringLiteral("pro-shoot-end-to-end")) ||
      !all_capability_signature.toString().contains(QStringLiteral("export-queue-controls")) ||
      !all_capability_signature.toString().contains(QStringLiteral("model-runtime-tests-matrix"))) {
    std::cerr << "Capability registry gate: "
              << capability_registry_gate_signature.toString().toStdString() << '\n';
    return fail("workspace should expose one duplicate-free capability registry gate for every claimed capability");
  }
  QVariant focus_peaking_tooltip;
  if (edit_workspace->property("complexTooltipCount").toInt() < 12 ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "complexToolTooltip",
          Q_RETURN_ARG(QVariant, focus_peaking_tooltip),
          Q_ARG(QVariant, QStringLiteral("focus-peaking"))) ||
      !focus_peaking_tooltip.toString().contains(QStringLiteral("edges")) ||
      focus_peaking_tooltip.toString().size() < 80) {
    return fail("workspace should expose high-quality tooltips for complex tools");
  }
  QVariant health_signature;
  QVariant health_detail_signature;
  QVariant health_count;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "systemHealthSignature",
          Q_RETURN_ARG(QVariant, health_signature)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "systemHealthDetailSignature",
          Q_RETURN_ARG(QVariant, health_detail_signature)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "systemHealthItemCount",
          Q_RETURN_ARG(QVariant, health_count)) ||
      health_count.toInt() != 4 ||
      !health_signature.toString().contains(QStringLiteral("GPU:")) ||
      !health_signature.toString().contains(QStringLiteral("Model:")) ||
      !health_signature.toString().contains(QStringLiteral("Catalog:")) ||
      !health_signature.toString().contains(QStringLiteral("Export:")) ||
      !health_detail_signature.toString().contains(editor_preview.smartSelectionRuntimeLabel()) ||
      !health_detail_signature.toString().contains(editor_preview.inpaintRuntimeLabel()) ||
      !health_detail_signature.toString().contains(QStringLiteral("root:")) ||
      !health_detail_signature.toString().contains(QStringLiteral("|sam:")) ||
      !health_detail_signature.toString().contains(QStringLiteral("|lama:")) ||
      !health_detail_signature.toString().contains(QStringLiteral("|onnx:")) ||
      !health_detail_signature.toString().contains(QStringLiteral("|vision:")) ||
      edit_workspace->findChild<QObject*>(QStringLiteral("healthStatusStrip")) == nullptr) {
    return fail("workspace should expose in-app health status for GPU, model, catalog, export, and model diagnostics");
  }
  QVariant histogram_available;
  QVariant waveform_available;
  QVariant parade_available;
  QVariant vectorscope_available;
  QVariant scope_channel_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "scopeAnalysisModeAvailable",
          Q_RETURN_ARG(QVariant, histogram_available),
          Q_ARG(QVariant, QStringLiteral("Histogram"))) ||
      !histogram_available.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "scopeAnalysisModeAvailable",
          Q_RETURN_ARG(QVariant, waveform_available),
          Q_ARG(QVariant, QStringLiteral("Waveform"))) ||
      !waveform_available.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "scopeAnalysisModeAvailable",
          Q_RETURN_ARG(QVariant, parade_available),
          Q_ARG(QVariant, QStringLiteral("Parade"))) ||
      !parade_available.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "scopeAnalysisModeAvailable",
          Q_RETURN_ARG(QVariant, vectorscope_available),
          Q_ARG(QVariant, QStringLiteral("Vectorscope"))) ||
      !vectorscope_available.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "scopeChannelSignature",
          Q_RETURN_ARG(QVariant, scope_channel_signature)) ||
      !scope_channel_signature.toString().contains(QStringLiteral("rgb:true")) ||
      !scope_channel_signature.toString().contains(QStringLiteral("luma:true"))) {
    return fail("workspace should expose RGB and luma histogram scope channels");
  }
  QVariant scope_layout_result;
  QVariant scope_sampling_result;
  QVariant scope_image_mode_result;
  QVariant scope_large_mode_result;
  QVariant invalid_scope_view_result;
  QVariant scope_view_signature;
  QVariant updated_scope_signature;
  QVariant proofing_status_signature;
  editor_preview.setOutputColorSpace(QStringLiteral("Display P3"));
  editor_preview.setSoftProofProfile(QStringLiteral("sRGB"));
  editor_preview.setSoftProofEnabled(true);
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("scopes"));
  edit_workspace->setProperty("largeScopesMode", false);
  QCoreApplication::processEvents();
  auto* inspector_scope_grid = find_named_object(edit_workspace, QStringLiteral("inspectorScopeGrid"));
  auto* scope_async_status_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("scopeAsyncStatusLabel"));
  auto* scope_proofing_status_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("scopeProofingStatusLabel"));
  auto* scopes_image_view_mode_button =
      find_named_object(edit_workspace, QStringLiteral("scopesImageViewModeButton"));
  auto* scopes_large_view_mode_button =
      find_named_object(edit_workspace, QStringLiteral("scopesLargeViewModeButton"));
  auto* large_scopes_view = find_named_object(edit_workspace, QStringLiteral("largeScopesView"));
  auto* large_scopes_image_mode_button =
      find_named_object(edit_workspace, QStringLiteral("largeScopesImageModeButton"));
  auto* large_scope_proofing_status_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("largeScopeProofingStatusLabel"));
  auto* scope_rgb_toggle = find_named_object(edit_workspace, QStringLiteral("scopeRgbToggle"));
  auto* scope_luma_toggle = find_named_object(edit_workspace, QStringLiteral("scopeLumaToggle"));
  auto* scope_density_toggle = find_named_object(edit_workspace, QStringLiteral("scopeDensityToggle"));
  auto* scope_trace_toggle = find_named_object(edit_workspace, QStringLiteral("scopeTraceToggle"));
  auto* scope_grid_toggle = find_named_object(edit_workspace, QStringLiteral("scopeGridToggle"));
  auto* scope_log_toggle = find_named_object(edit_workspace, QStringLiteral("scopeLogToggle"));
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setScopeLayoutPreset",
          Q_RETURN_ARG(QVariant, scope_layout_result),
          Q_ARG(QVariant, QStringLiteral("Quad"))) ||
      !scope_layout_result.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setScopeSamplingMode",
          Q_RETURN_ARG(QVariant, scope_sampling_result),
          Q_ARG(QVariant, QStringLiteral("Precise"))) ||
      !scope_sampling_result.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "scopeChannelSignature",
          Q_RETURN_ARG(QVariant, updated_scope_signature)) ||
      !updated_scope_signature.toString().contains(QStringLiteral("layout:Quad")) ||
      !updated_scope_signature.toString().contains(QStringLiteral("sampling:Precise")) ||
      inspector_scope_grid == nullptr ||
      inspector_scope_grid->property("effectiveColumns").toInt() != 1 ||
      scope_async_status_label == nullptr ||
      !scope_async_status_label->property("text").toString().contains(QStringLiteral("Scopes")) ||
      scope_proofing_status_label == nullptr ||
      !scope_proofing_status_label->property("text").toString().contains(QStringLiteral("Output Display P3")) ||
      !scope_proofing_status_label->property("text").toString().contains(QStringLiteral("Proof sRGB")) ||
      large_scope_proofing_status_label == nullptr ||
      !large_scope_proofing_status_label->property("text").toString().contains(QStringLiteral("Output Display P3")) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "proofingStatusSignature",
          Q_RETURN_ARG(QVariant, proofing_status_signature)) ||
      proofing_status_signature.toString() !=
          QStringLiteral("output:Display P3|proof:true|profile:sRGB") ||
      scopes_image_view_mode_button == nullptr || scopes_large_view_mode_button == nullptr ||
      large_scopes_view == nullptr || large_scopes_image_mode_button == nullptr ||
      scope_rgb_toggle == nullptr || scope_luma_toggle == nullptr ||
      scope_density_toggle == nullptr || scope_trace_toggle == nullptr ||
      scope_grid_toggle == nullptr || scope_log_toggle == nullptr) {
    return fail("workspace should expose scope layout presets and sampling controls");
  }
  const auto scope_recipe_before_toggles = editor_preview.currentRecipeJson();
  if (!QMetaObject::invokeMethod(scope_rgb_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_luma_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_density_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_trace_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_grid_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_log_toggle, "click")) {
    return fail("scope display toggles should be reachable as inspector controls");
  }
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "scopeChannelSignature",
          Q_RETURN_ARG(QVariant, updated_scope_signature)) ||
      !updated_scope_signature.toString().contains(QStringLiteral("rgb:false")) ||
      !updated_scope_signature.toString().contains(QStringLiteral("luma:false")) ||
      !updated_scope_signature.toString().contains(QStringLiteral("density:false")) ||
      !updated_scope_signature.toString().contains(QStringLiteral("trace:false")) ||
      !updated_scope_signature.toString().contains(QStringLiteral("grid:false")) ||
      !updated_scope_signature.toString().contains(QStringLiteral("log:true")) ||
      editor_preview.currentRecipeJson() != scope_recipe_before_toggles) {
    return fail("scope display toggles should change only scope presentation state, not the edit recipe");
  }
  if (!QMetaObject::invokeMethod(scope_rgb_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_luma_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_density_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_trace_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_grid_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_log_toggle, "click")) {
    return fail("scope display toggles should be reversible from the inspector controls");
  }
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setScopesViewMode",
          Q_RETURN_ARG(QVariant, scope_image_mode_result),
          Q_ARG(QVariant, QStringLiteral("image"))) ||
      !scope_image_mode_result.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "scopesViewModeSignature",
          Q_RETURN_ARG(QVariant, scope_view_signature)) ||
      scope_view_signature.toString() != QStringLiteral("mode:image|image:true|large:false") ||
      !edit_image_viewport->property("visible").toBool() ||
      large_scopes_view->property("visible").toBool() ||
      !scopes_image_view_mode_button->property("active").toBool() ||
      scopes_large_view_mode_button->property("active").toBool()) {
    return fail("scope image view mode should keep the photo visible and mark Image as the active mode");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setScopesViewMode",
          Q_RETURN_ARG(QVariant, scope_large_mode_result),
          Q_ARG(QVariant, QStringLiteral("large"))) ||
      !scope_large_mode_result.toBool()) {
    return fail("workspace should expose an explicit large scopes view mode");
  }
  QCoreApplication::processEvents();
  auto* large_scope_grid = find_named_object(edit_workspace, QStringLiteral("largeScopeGrid"));
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "scopesViewModeSignature",
          Q_RETURN_ARG(QVariant, scope_view_signature)) ||
      scope_view_signature.toString() != QStringLiteral("mode:large|image:false|large:true") ||
      large_scope_grid == nullptr || large_scope_grid->property("effectiveColumns").toInt() != 2 ||
      edit_image_viewport->property("visible").toBool() ||
      !large_scopes_view->property("visible").toBool() ||
      edit_workspace->property("editInspectorTab").toString() != QStringLiteral("scopes") ||
      edit_workspace->property("editContextPanelActive").toBool() ||
      scopes_image_view_mode_button->property("active").toBool() ||
      !scopes_large_view_mode_button->property("active").toBool()) {
    return fail("large scope mode should be explicit, hide the photo only in scopes-only view, and release context-panel space");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setScopesViewMode",
          Q_RETURN_ARG(QVariant, invalid_scope_view_result),
          Q_ARG(QVariant, QStringLiteral("bogus"))) ||
      invalid_scope_view_result.toBool() ||
      edit_workspace->property("largeScopesMode").toBool() != true) {
    return fail("invalid scope view modes should be rejected without changing the active mode");
  }
  if (!QMetaObject::invokeMethod(large_scopes_image_mode_button, "click")) {
    return fail("large scopes view should expose a direct return-to-image action");
  }
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "scopesViewModeSignature",
          Q_RETURN_ARG(QVariant, scope_view_signature)) ||
      scope_view_signature.toString() != QStringLiteral("mode:image|image:true|large:false") ||
      !edit_image_viewport->property("visible").toBool() ||
      large_scopes_view->property("visible").toBool()) {
    return fail("returning from large scopes should restore the image viewport");
  }

  catalog_assets.replaceAssetsForTesting({
      make_asset(
          QStringLiteral("visual-proof-1"),
          viewport_asset_path,
          QUrl::fromLocalFile(viewport_asset_path).toString()),
      make_asset(
          QStringLiteral("visual-proof-2"),
          viewport_asset_path,
          QUrl::fromLocalFile(viewport_asset_path).toString()),
      make_asset(
          QStringLiteral("visual-proof-3"),
          viewport_asset_path,
          QUrl::fromLocalFile(viewport_asset_path).toString()),
      make_asset(
          QStringLiteral("overlay-proof-bright"),
          bright_overlay_asset_path,
          QUrl::fromLocalFile(bright_overlay_asset_path).toString()),
      make_asset(
          QStringLiteral("overlay-proof-dark"),
          dark_overlay_asset_path,
          QUrl::fromLocalFile(dark_overlay_asset_path).toString()),
      make_asset(
          QStringLiteral("overlay-proof-saturated"),
          saturated_overlay_asset_path,
          QUrl::fromLocalFile(saturated_overlay_asset_path).toString()),
      make_asset(
          QStringLiteral("overlay-proof-transparent"),
          transparent_overlay_asset_path,
          QUrl::fromLocalFile(transparent_overlay_asset_path).toString()),
  });
  catalog_assets.selectIndex(0);
  if (!wait_for_condition([&]() {
        return editor_preview.hasAsset() &&
               editor_preview.assetId() == QStringLiteral("visual-proof-1");
      })) {
    return fail("desktop visual proof should load a seeded filmstrip asset");
  }
  edit_workspace->setProperty("diagnosticsPanelOpen", false);
  edit_workspace->setProperty("editContextPanelVisible", true);
  edit_workspace->setProperty("editSidebarCollapsed", false);
  edit_workspace->setProperty("editInspectorPanelVisible", true);
  edit_workspace->setProperty("largeScopesMode", false);
  edit_workspace->setProperty("editAdjustmentFilterText", QString());
  QMetaObject::invokeMethod(
      edit_workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
  process_layout_events();
  if (!capture_visual_proof_pair(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("adjust-with-filmstrip"),
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("editFilmstrip"),
              QStringLiteral("editFilmstripThumbnailCard"),
              QStringLiteral("editInspectorPanel"),
              QStringLiteral("adjustInlineHistogramScope"),
              QStringLiteral("adjustInlineWaveformScope"),
              QStringLiteral("lightInspectorGroup"),
          },
          &visual_proof_manifest)) {
    return fail("desktop visual proof should capture Adjust with pinned scopes and filmstrip");
  }

  edit_workspace->setProperty("editInspectorTab", QStringLiteral("presets"));
  edit_workspace->setProperty("studioPresetCategoryFilter", QStringLiteral("All"));
  edit_workspace->setProperty("studioPresetPackFilter", QStringLiteral("All"));
  process_layout_events();
  if (!capture_visual_proof_pair(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("presets-tab"),
          {
              QStringLiteral("editInspectorPanel"),
              QStringLiteral("presetsInspectorTabButton"),
              QStringLiteral("presetLibraryInspectorGroup"),
              QStringLiteral("studioPresetLibraryList"),
              QStringLiteral("studioPresetLibraryCard"),
          },
          &visual_proof_manifest)) {
    return fail("desktop visual proof should capture the Presets tab");
  }

  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  edit_workspace->setProperty("editAdjustmentFilterText", QStringLiteral("grading"));
  edit_workspace->setProperty("splitToningExpanded", true);
  process_layout_events();
  if (!capture_visual_proof_pair(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("color-grading"),
          {
              QStringLiteral("editInspectorPanel"),
              QStringLiteral("advancedColorInspectorGroup"),
              QStringLiteral("colorGradingInspectorGroup"),
              QStringLiteral("splitToningGroup"),
              QStringLiteral("colorGradingAmountSlider"),
              QStringLiteral("colorGradingBalanceSlider"),
          },
          &visual_proof_manifest)) {
    return fail("desktop visual proof should capture Color Grading controls");
  }

  edit_workspace->setProperty("editAdjustmentFilterText", QStringLiteral("curve"));
  edit_workspace->setProperty("lightAdvancedToneExpanded", true);
  edit_workspace->setProperty("lightLevelsExpanded", true);
  process_layout_events();
  if (!capture_visual_proof_pair(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("tone-curve-input-levels"),
          {
              QStringLiteral("editInspectorPanel"),
              QStringLiteral("toneCurveInspectorGroup"),
              QStringLiteral("studioToneCurveEditor"),
              QStringLiteral("toneCurveInputLevelsGroup"),
              QStringLiteral("toneCurveInputLevelsHistogram"),
              QStringLiteral("toneCurveInputLevelsHandle"),
          },
          &visual_proof_manifest)) {
    return fail("desktop visual proof should capture Tone Curve and Input Levels");
  }

  edit_workspace->setProperty("editAdjustmentFilterText", QString());
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("scopes"));
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setScopesViewMode",
          Q_RETURN_ARG(QVariant, scope_image_mode_result),
          Q_ARG(QVariant, QStringLiteral("image"))) ||
      !scope_image_mode_result.toBool()) {
    return fail("desktop visual proof should set pinned scopes image mode");
  }
  process_layout_events();
  if (!capture_visual_proof_pair(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("scopes-pinned"),
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("editInspectorPanel"),
              QStringLiteral("scopeAsyncStatusLabel"),
              QStringLiteral("scopeProofingStatusLabel"),
              QStringLiteral("scopeRgbToggle"),
              QStringLiteral("scopesLargeViewModeButton"),
          },
          &visual_proof_manifest)) {
    return fail("desktop visual proof should capture pinned Scopes while the image stays visible");
  }

  edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
  process_layout_events();
  if (!capture_visual_proof_pair(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("layers-tab"),
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("editInspectorPanel"),
              QStringLiteral("layersInspectorTabButton"),
              QStringLiteral("photoLayersInspectorGroup"),
          },
          &visual_proof_manifest)) {
    return fail("desktop visual proof should capture Layers");
  }

  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "startMaskBrushSource",
          Q_ARG(QVariant, QVariant(QStringLiteral("add"))))) {
    return fail("desktop visual proof should start brush mode");
  }
  editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
  editor_preview.beginActiveBrushStroke();
  editor_preview.addBrushPointWithTablet(0.46, 0.54, 0.72, 0.0, 0.0);
  process_layout_events();
  if (!capture_visual_proof_pair(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("brush-mode"),
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("brushToolHud"),
              QStringLiteral("brushHudStatusLabel"),
              QStringLiteral("brushContextPanel"),
              QStringLiteral("editImageViewportMaskCanvasOverlay"),
          },
          &visual_proof_manifest)) {
    editor_preview.endActiveBrushStroke();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return fail("desktop visual proof should capture Brush mode");
  }
  editor_preview.endActiveBrushStroke();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);

  QMetaObject::invokeMethod(
      edit_workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "createMaskSource",
          Q_ARG(QVariant, QVariant(QStringLiteral("object"))))) {
    return fail("desktop visual proof should start object selection");
  }
  process_layout_events();
  if (!capture_visual_proof_pair(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("object-selection"),
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("maskCreationToolbar"),
              QStringLiteral("maskSourceObjectButton"),
              QStringLiteral("editImageViewportMaskCanvasOverlay"),
          },
          &visual_proof_manifest)) {
    return fail("desktop visual proof should capture Object Selection");
  }

  auto capture_overlay_mode_visual_proof = [&](
                                               int asset_index,
                                               const QString& asset_id,
                                               const QString& fixture_kind,
                                               const QString& preview_mode,
                                               const QString& state,
                                               const QStringList& required_objects) {
    catalog_assets.selectIndex(asset_index);
    if (!wait_for_condition([&]() {
          return editor_preview.hasAsset() && editor_preview.assetId() == asset_id;
        })) {
      std::cerr << "Overlay visual proof should load asset " << asset_id.toStdString() << '\n';
      return false;
    }
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "createMaskSource",
            Q_ARG(QVariant, QVariant(QStringLiteral("rectangle"))))) {
      std::cerr << "Overlay visual proof should create a rectangle selection for "
                << asset_id.toStdString() << '\n';
      return false;
    }
    editor_preview.setActiveMaskCenterX(0.5);
    editor_preview.setActiveMaskCenterY(0.5);
    editor_preview.setActiveMaskRadiusX(0.28);
    editor_preview.setActiveMaskRadiusY(0.22);
    editor_preview.setActiveMaskFeather(0.035);
    editor_preview.setMaskPreviewOverlayOpacity(1.0);
    editor_preview.setMaskPreviewOverlayColor(QStringLiteral("#24dcc2"));
    editor_preview.setMaskPreviewMode(preview_mode);
    process_layout_events(18);
    QJsonObject metadata;
    metadata.insert(QStringLiteral("fixture"), fixture_kind);
    metadata.insert(QStringLiteral("maskPreviewMode"), editor_preview.maskPreviewMode());
    metadata.insert(QStringLiteral("activeMaskKind"), editor_preview.activeMaskKind());
    metadata.insert(QStringLiteral("assetId"), editor_preview.assetId());
    return capture_visual_proof_pair(
        root_window,
        root,
        visual_proof_dir,
        state,
        required_objects,
        &visual_proof_manifest,
        metadata);
  };

  if (!capture_overlay_mode_visual_proof(
          3,
          QStringLiteral("overlay-proof-bright"),
          QStringLiteral("bright"),
          QStringLiteral("overlay"),
          QStringLiteral("mask-overlay-bright-color-overlay"),
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("editImageViewportSharedMaskOverlay"),
              QStringLiteral("editImageViewportMaskCanvasOverlay"),
          })) {
    return fail("desktop visual proof should capture color overlay mode on a bright image");
  }
  if (!capture_overlay_mode_visual_proof(
          4,
          QStringLiteral("overlay-proof-dark"),
          QStringLiteral("dark"),
          QStringLiteral("marching-ants"),
          QStringLiteral("mask-overlay-dark-marching-ants"),
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("editImageViewportSharedMaskOverlay"),
              QStringLiteral("editImageViewportSharedActiveSelectionOutline"),
          })) {
    return fail("desktop visual proof should capture marching ants mode on a dark image");
  }
  if (!capture_overlay_mode_visual_proof(
          5,
          QStringLiteral("overlay-proof-saturated"),
          QStringLiteral("high-saturation"),
          QStringLiteral("color-on-bw"),
          QStringLiteral("mask-overlay-saturated-color-on-bw"),
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("editImageViewportSharedMaskOverlay"),
              QStringLiteral("editImageViewportMaskCanvasOverlay"),
              QStringLiteral("editImageViewportSharedActiveSelectionOutline"),
          })) {
    return fail("desktop visual proof should capture Color on B&W mode on a high-saturation image");
  }
  if (!capture_overlay_mode_visual_proof(
          6,
          QStringLiteral("overlay-proof-transparent"),
          QStringLiteral("transparent"),
          QStringLiteral("on-white"),
          QStringLiteral("mask-overlay-transparent-on-white"),
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("editImageViewportSharedMaskOverlay"),
              QStringLiteral("editImageViewportMaskCanvasOverlay"),
          })) {
    return fail("desktop visual proof should capture On White mode on a transparent image");
  }

  QMetaObject::invokeMethod(
      edit_workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  edit_workspace->setProperty("editAdjustmentFilterText", QStringLiteral("detail"));
  process_layout_events();
  if (!capture_visual_proof_pair(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("detail-controls"),
          {
              QStringLiteral("editInspectorPanel"),
              QStringLiteral("detailInspectorGroup"),
              QStringLiteral("detailSharpeningGroup"),
              QStringLiteral("detailNoiseReductionGroup"),
              QStringLiteral("detailInspectOneToOneButton"),
          },
          &visual_proof_manifest)) {
    return fail("desktop visual proof should capture Detail controls");
  }

  edit_workspace->setProperty("editAdjustmentFilterText", QString());
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  edit_workspace->setProperty("largeScopesMode", false);
  QMetaObject::invokeMethod(
      edit_workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
  root->setProperty("width", 1600);
  root->setProperty("height", 1000);
  process_layout_events();
  const auto visual_manifest_path = QDir(visual_proof_dir).filePath(QStringLiteral("manifest.json"));
  QFile visual_manifest_file(visual_manifest_path);
  if (!visual_manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      visual_manifest_file.write(QJsonDocument(visual_proof_manifest).toJson(QJsonDocument::Indented)) <=
          0) {
    return fail("desktop visual proof should write a manifest");
  }
  visual_manifest_file.close();
  if (visual_proof_manifest.size() != 26) {
    return fail("desktop visual proof should capture thirteen required editor states at desktop and compact widths");
  }
  std::cout << "desktop_visual_proof_dir=" << visual_proof_dir.toStdString() << '\n';

  root->setProperty("currentWorkspace", root->property("libraryWorkspaceIndex"));
  QCoreApplication::processEvents();

  catalog_assets.replaceAssetsForTesting(
      {make_asset(QStringLiteral("survey-1"), QStringLiteral("IMG_0101.jpg")),
       make_asset(QStringLiteral("survey-2"), QStringLiteral("IMG_0102.jpg")),
       make_asset(QStringLiteral("survey-3"), QStringLiteral("IMG_0103.jpg")),
       make_asset(QStringLiteral("survey-4"), QStringLiteral("IMG_0104.jpg"))});
  catalog_assets.selectIndex(1);
  QCoreApplication::processEvents();
  auto* library_workspace = root->findChild<QObject*>(QStringLiteral("libraryStudioWorkspace"));
  if (library_workspace == nullptr) {
    return fail("library workspace should expose a testable StudioWorkspace object");
  }
  auto* library_sources_panel = library_workspace->findChild<QObject*>(QStringLiteral("librarySourcesPanel"));
  auto* library_inspector_panel = library_workspace->findChild<QObject*>(QStringLiteral("libraryInspectorPanel"));
  if (library_sources_panel == nullptr || library_inspector_panel == nullptr ||
      library_sources_panel->property("title").toString() != QStringLiteral("Sources") ||
      library_inspector_panel->property("title").toString() != QStringLiteral("Inspector")) {
    return fail("library side panels should use readable title-case labels");
  }
  library_workspace->setProperty("catalogViewMode", QStringLiteral("survey"));
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  if (catalog_assets.surveyAssetCount(catalog_assets.selectedIndex(), 6) < 3 ||
      library_workspace->property("catalogViewMode").toString() != QStringLiteral("survey")) {
    return fail("library workspace should expose a populated survey view model");
  }

  if (!QMetaObject::invokeMethod(root, "beginImportWorkspace")) {
    return fail("menu routing should expose beginImportWorkspace");
  }
  QCoreApplication::processEvents();
  if (root->property("currentWorkspace").toInt() != 0 ||
      root->property("libraryWorkspaceMode").toString() != QStringLiteral("import")) {
    return fail("beginImportWorkspace should route to Library import mode");
  }

  if (!QMetaObject::invokeMethod(root, "createTimelineFromMenu")) {
    return fail("menu routing should expose createTimelineFromMenu");
  }
  QCoreApplication::processEvents();
  if (root->property("currentWorkspace").toInt() != root->property("timelineWorkspaceIndex").toInt() ||
      timeline_document.timelineName().trimmed().isEmpty()) {
    return fail("createTimelineFromMenu should route to Timeline and initialize a document");
  }

  const auto library_workspace_index = root->property("libraryWorkspaceIndex").toInt();
  const auto edit_workspace_index = root->property("editWorkspaceIndex").toInt();
  const auto carousel_workspace_index = root->property("carouselWorkspaceIndex").toInt();
  const auto timeline_workspace_index = root->property("timelineWorkspaceIndex").toInt();
  const auto export_workspace_index = root->property("exportWorkspaceIndex").toInt();
  const QList<int> expected_workspace_order = {
      library_workspace_index,
      edit_workspace_index,
      carousel_workspace_index,
      timeline_workspace_index,
      export_workspace_index,
  };
  for (auto number = 1; number <= expected_workspace_order.size(); ++number) {
    QVariant returned_value;
    const auto invoked = QMetaObject::invokeMethod(
        root,
        "switchWorkspaceByNumber",
        Q_RETURN_ARG(QVariant, returned_value),
        Q_ARG(QVariant, number));
    QCoreApplication::processEvents();
    if (!invoked || !returned_value.toBool() ||
        root->property("currentWorkspace").toInt() != expected_workspace_order.at(number - 1)) {
      return fail("global workspace number shortcuts should route across every workspace");
    }
  }
  auto* export_workspace = root->findChild<QObject*>(QStringLiteral("exportStudioWorkspace"));
  auto* export_presets_panel = export_workspace != nullptr
      ? export_workspace->findChild<QObject*>(QStringLiteral("exportPresetsPanel"))
      : nullptr;
  auto* export_settings_panel = export_workspace != nullptr
      ? export_workspace->findChild<QObject*>(QStringLiteral("exportSettingsPanel"))
      : nullptr;
  if (export_workspace == nullptr || export_presets_panel == nullptr || export_settings_panel == nullptr ||
      export_presets_panel->property("title").toString() != QStringLiteral("Export Presets") ||
      export_settings_panel->property("title").toString() != QStringLiteral("Output Settings")) {
    return fail("export panels should use readable title-case labels");
  }
  QVariant invalid_workspace_shortcut;
  const auto invalid_invoked = QMetaObject::invokeMethod(
      root,
      "switchWorkspaceByNumber",
      Q_RETURN_ARG(QVariant, invalid_workspace_shortcut),
      Q_ARG(QVariant, expected_workspace_order.size() + 1));
  if (!invalid_invoked || invalid_workspace_shortcut.toBool()) {
    return fail("global workspace shortcut routing should reject unknown workspace numbers");
  }
  root->setProperty("currentWorkspace", edit_workspace_index);
  QCoreApplication::processEvents();
  if (root->property("activityIndicatorBottomMargin").toInt() < 96) {
    return fail("edit workspace activity indicator should clear the filmstrip and footer controls");
  }
  const auto edit_indicator_right_margin = root->property("activityIndicatorRightMargin").toInt();
  const auto edit_inspector_panel_width = edit_workspace->property("editInspectorPanelWidth").toInt();
  if (edit_indicator_right_margin < edit_inspector_panel_width + 20) {
    return fail("edit workspace activity indicator should clear the right inspector controls");
  }
  root->setProperty("currentWorkspace", library_workspace_index);
  QCoreApplication::processEvents();
  if (root->property("activityIndicatorBottomMargin").toInt() != 12 ||
      root->property("activityIndicatorRightMargin").toInt() != 12) {
    return fail("non-edit activity indicator should stay close to the workspace edge");
  }
  QVariant export_cancel_supported;
  QVariant preview_cancel_supported;
  QVariant running_export_cancel_policy;
  QVariant running_import_cancel_policy;
  QVariant queued_model_cancel_policy;
  QVariant activity_signature;
  QVariant preview_activity_presentation;
  QVariant export_failure_presentation;
  if (!QMetaObject::invokeMethod(
          root,
          "supportsLongOperationCancel",
          Q_RETURN_ARG(QVariant, export_cancel_supported),
          Q_ARG(QVariant, QStringLiteral("asset_export"))) ||
      !export_cancel_supported.toBool() ||
      !QMetaObject::invokeMethod(
          root,
          "supportsLongOperationCancel",
          Q_RETURN_ARG(QVariant, preview_cancel_supported),
          Q_ARG(QVariant, QStringLiteral("preview_render"))) ||
      preview_cancel_supported.toBool() ||
      !QMetaObject::invokeMethod(
          root,
          "longOperationCancellationPolicySignature",
          Q_RETURN_ARG(QVariant, running_export_cancel_policy),
          Q_ARG(QVariant, QStringLiteral("asset_export")),
          Q_ARG(QVariant, QStringLiteral("Running"))) ||
      running_export_cancel_policy.toString() !=
          QStringLiteral("asset_export|Running|cancel:true|mode:cooperative") ||
      !QMetaObject::invokeMethod(
          root,
          "longOperationCancellationPolicySignature",
          Q_RETURN_ARG(QVariant, running_import_cancel_policy),
          Q_ARG(QVariant, QStringLiteral("catalog_import")),
          Q_ARG(QVariant, QStringLiteral("Running"))) ||
      running_import_cancel_policy.toString() !=
          QStringLiteral("catalog_import|Running|cancel:false|mode:unsupported") ||
      !QMetaObject::invokeMethod(
          root,
          "longOperationCancellationPolicySignature",
          Q_RETURN_ARG(QVariant, queued_model_cancel_policy),
          Q_ARG(QVariant, QStringLiteral("ai_model_inference")),
          Q_ARG(QVariant, QStringLiteral("Queued"))) ||
      queued_model_cancel_policy.toString() !=
          QStringLiteral("ai_model_inference|Queued|cancel:true|mode:queued-drop") ||
      !QMetaObject::invokeMethod(
          root,
          "primaryActivityControlSignature",
          Q_RETURN_ARG(QVariant, activity_signature)) ||
      !activity_signature.toString().contains(QStringLiteral("|")) ||
      !QMetaObject::invokeMethod(
          root,
          "activityPresentationSignature",
          Q_RETURN_ARG(QVariant, preview_activity_presentation),
          Q_ARG(QVariant, QStringLiteral("preview_render")),
          Q_ARG(QVariant, -1),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, 0)) ||
      preview_activity_presentation.toString() !=
          QStringLiteral("preview_render|blocking:false|progress:indeterminate|cancel:false|retry:false") ||
      !QMetaObject::invokeMethod(
          root,
          "activityPresentationSignature",
          Q_RETURN_ARG(QVariant, export_failure_presentation),
          Q_ARG(QVariant, QStringLiteral("asset_export")),
          Q_ARG(QVariant, 2.0),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, 1)) ||
      export_failure_presentation.toString() !=
          QStringLiteral("asset_export|blocking:true|progress:1.000|cancel:true|retry:true") ||
      root->findChild<QObject*>(QStringLiteral("floatingActivityIndicator")) == nullptr ||
      root->findChild<QObject*>(QStringLiteral("activityCancelButton")) == nullptr) {
    std::cerr << "exportCancelSupported=" << export_cancel_supported.toString().toStdString()
              << " previewCancelSupported=" << preview_cancel_supported.toString().toStdString()
              << " runningExportPolicy=" << running_export_cancel_policy.toString().toStdString()
              << " runningImportPolicy=" << running_import_cancel_policy.toString().toStdString()
              << " queuedModelPolicy=" << queued_model_cancel_policy.toString().toStdString()
              << " activitySignature=" << activity_signature.toString().toStdString()
              << " previewPresentation=" << preview_activity_presentation.toString().toStdString()
              << " exportFailurePresentation=" << export_failure_presentation.toString().toStdString()
              << " floatingActivity="
              << (root->findChild<QObject*>(QStringLiteral("floatingActivityIndicator")) != nullptr)
              << " activityButton="
              << (root->findChild<QObject*>(QStringLiteral("activityCancelButton")) != nullptr)
              << '\n';
    return fail("app should expose visible progress and cancel controls for long operations");
  }
  auto failed_export_job = make_persistent_job(
      QStringLiteral("ui-export-failed"),
      QStringLiteral("asset_export"),
      QStringLiteral("Failed"),
      QStringLiteral("Disk full"));
  if (!photo_editor::desktop::upsertPersistedJobRecord(failed_export_job, nullptr)) {
    return fail("desktop shell smoke should seed a failed export job");
  }
  job_queue.refresh();
  root->setProperty("currentWorkspace", export_workspace_index);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* failure_activity_indicator =
      root->findChild<QObject*>(QStringLiteral("failureActivityIndicator"));
  auto* failure_retry_button = failure_activity_indicator != nullptr
      ? failure_activity_indicator->findChild<QObject*>(QStringLiteral("activityCancelButton"))
      : nullptr;
  if (failure_activity_indicator == nullptr || failure_retry_button == nullptr ||
      !failure_activity_indicator->property("visible").toBool() ||
      failure_retry_button->property("text").toString() != QStringLiteral("Retry") ||
      root->property("primaryFailureKind").toString() != QStringLiteral("asset_export") ||
      !root->property("primaryFailureRetryable").toBool() ||
      !root->property("primaryFailureDetail").toString().contains(QStringLiteral("Disk full"))) {
    return fail("failed export jobs should surface a compact retry status instead of cluttering the canvas");
  }
  QVariant retry_failure_result;
  if (!QMetaObject::invokeMethod(
          root,
          "retryPrimaryFailure",
          Q_RETURN_ARG(QVariant, retry_failure_result)) ||
      !retry_failure_result.toBool()) {
    return fail("failed export retry status should dispatch to the persisted job queue");
  }
  job_queue.refresh();
  QCoreApplication::processEvents();
  if (job_queue.failedJobCountForKind(QStringLiteral("asset_export")) != 0 ||
      job_queue.activeJobCountForKind(QStringLiteral("asset_export")) < 1 ||
      root->property("primaryFailureKind").toString() != QStringLiteral("none")) {
    return fail("retrying a failed export from status UI should requeue the job and clear failure chrome");
  }
  auto* command_palette = root->findChild<QObject*>(QStringLiteral("commandPaletteDialog"));
  QVariant command_coverage_signature;
  QVariant missing_command_labels;
  if (command_palette == nullptr || command_palette->property("commandCount").toInt() < 68 ||
      command_palette->property("requiredCommandCount").toInt() < 27 ||
      !QMetaObject::invokeMethod(
          command_palette,
          "commandCoverageSignature",
          Q_RETURN_ARG(QVariant, command_coverage_signature)) ||
      !command_coverage_signature.toString().startsWith(QStringLiteral("commands:")) ||
      !command_coverage_signature.toString().contains(QStringLiteral("|missing:")) ||
      !command_coverage_signature.toString().endsWith(QStringLiteral("|missing:")) ||
      !QMetaObject::invokeMethod(
          command_palette,
          "missingRequiredCommandLabels",
          Q_RETURN_ARG(QVariant, missing_command_labels)) ||
      !missing_command_labels.toList().isEmpty()) {
    std::cerr << "Command palette coverage: "
              << command_coverage_signature.toString().toStdString() << '\n';
    return fail("command palette should include every required workspace command and primary tool");
  }
  QVariant crop_tool_activated;
  if (!QMetaObject::invokeMethod(
          root,
          "activatePrimaryEditTool",
          Q_RETURN_ARG(QVariant, crop_tool_activated),
          Q_ARG(QVariant, QStringLiteral("crop"))) ||
      !crop_tool_activated.toBool() || root->property("currentWorkspace").toInt() !=
          root->property("editWorkspaceIndex").toInt() ||
      root->property("editWorkspaceMode").toString() != QStringLiteral("crop")) {
    return fail("primary edit tool commands should dispatch through the same workspace activation path");
  }
  QMetaObject::invokeMethod(
      root,
      "activatePrimaryEditTool",
      Q_ARG(QVariant, QStringLiteral("adjust")));
  QVariant workflow_shortcut_signature;
  const auto workflow_shortcut_prefix =
      root->property("macStyle").toBool() ? QStringLiteral("Meta+Alt+") : QStringLiteral("Ctrl+Alt+");
  const auto expected_workflow_shortcut_signature =
      QStringLiteral("adjust:%1A|crop:%1C|masks:%1M|brush:%1B|compare:%1P|export:%1E")
          .arg(workflow_shortcut_prefix);
  if (!QMetaObject::invokeMethod(
          root,
          "coreWorkflowShortcutSignature",
          Q_RETURN_ARG(QVariant, workflow_shortcut_signature)) ||
      workflow_shortcut_signature.toString() != expected_workflow_shortcut_signature) {
    std::cerr << "Core workflow shortcuts: "
              << workflow_shortcut_signature.toString().toStdString() << '\n';
    return fail("core edit, crop, mask, compare, and export workflows should have deterministic shortcuts");
  }
  auto* keyboard_shortcuts = root->findChild<QObject*>(QStringLiteral("keyboardShortcutsDialog"));
  QVariant edited_shortcut;
  QVariant reset_shortcut;
  QVariant library_shortcut_sequence;
  QVariant would_conflict;
  QVariant rejected_conflict;
  QVariant editable_workflow_shortcut_signature;
  QVariant mask_shortcut_help_signature;
  QVariant brush_shortcut_help_signature;
  const auto editable_shortcut_prefix =
      root->property("macStyle").toBool() ? QStringLiteral("Cmd+Alt+") : QStringLiteral("Ctrl+Alt+");
  const auto expected_editable_workflow_shortcut_signature =
      QStringLiteral("adjust:%1A|crop:%1C|masks:%1M|brush:%1B|compare:%1P|export:%1E")
          .arg(editable_shortcut_prefix);
  if (keyboard_shortcuts == nullptr ||
      keyboard_shortcuts->property("editableShortcutCount").toInt() < 11 ||
      !QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "coreWorkflowShortcutSignature",
          Q_RETURN_ARG(QVariant, editable_workflow_shortcut_signature)) ||
      editable_workflow_shortcut_signature.toString() != expected_editable_workflow_shortcut_signature ||
      !QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "setShortcutSequence",
          Q_RETURN_ARG(QVariant, edited_shortcut),
          Q_ARG(QVariant, QStringLiteral("command-palette")),
          Q_ARG(QVariant, QStringLiteral("Ctrl+Alt+K"))) ||
      !edited_shortcut.toBool() ||
      !QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "resetShortcutSequence",
           Q_RETURN_ARG(QVariant, reset_shortcut),
           Q_ARG(QVariant, QStringLiteral("command-palette"))) ||
      !reset_shortcut.toBool()) {
    return fail("keyboard shortcuts dialog should expose editable shortcut mappings");
  }
  const auto expected_mask_shortcut_help_signature =
      QStringLiteral("create:Ctrl+Shift+M|add-subtract-intersect:Ctrl+Shift+A/S/I|"
                     "invert:Ctrl+Shift+V|overlay:Ctrl+Shift+O|outline:Ctrl+Shift+U|"
                     "layer-mask-target-next:Ctrl+Shift+L/K|"
                     "layer-mask-target-previous:Ctrl+Alt+Shift+L/K|base-image:Ctrl+Shift+B|"
                     "temporary-erase:X-hold|temporary-hide:H-hold|rename:F2");
  if (!QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "maskShortcutHelpSignature",
          Q_RETURN_ARG(QVariant, mask_shortcut_help_signature)) ||
      mask_shortcut_help_signature.toString() != expected_mask_shortcut_help_signature) {
    std::cerr << "Mask shortcut help: "
              << mask_shortcut_help_signature.toString().toStdString() << '\n';
    return fail("keyboard shortcuts dialog should document the active mask, layer target, outline, and temporary brush shortcut contract");
  }
  const auto expected_brush_shortcut_help_signature =
      QStringLiteral("brush-add:B|brush-erase:E|temporary-erase:X-hold|size:[/]|"
                     "hardness:Shift+[/Shift+]|flow:Alt+[/Alt+]|density:Ctrl+[/Ctrl+]|"
                     "opacity:Ctrl+Alt+[/Ctrl+Alt+]");
  if (!QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "brushShortcutHelpSignature",
          Q_RETURN_ARG(QVariant, brush_shortcut_help_signature)) ||
      brush_shortcut_help_signature.toString() != expected_brush_shortcut_help_signature) {
    std::cerr << "Brush shortcut help: "
              << brush_shortcut_help_signature.toString().toStdString() << '\n';
    return fail("keyboard shortcuts dialog should document active brush size, hardness, flow, density, opacity, and temporary erase shortcuts");
  }
  if (!QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "shortcutSequence",
          Q_RETURN_ARG(QVariant, library_shortcut_sequence),
          Q_ARG(QVariant, QStringLiteral("workspace-library"))) ||
      library_shortcut_sequence.toString().trimmed().isEmpty() ||
      !QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "shortcutWouldConflict",
          Q_RETURN_ARG(QVariant, would_conflict),
          Q_ARG(QVariant, QStringLiteral("workspace-edit")),
          Q_ARG(QVariant, library_shortcut_sequence.toString())) ||
      !would_conflict.toBool() ||
      !QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "setShortcutSequence",
          Q_RETURN_ARG(QVariant, rejected_conflict),
          Q_ARG(QVariant, QStringLiteral("workspace-edit")),
          Q_ARG(QVariant, library_shortcut_sequence.toString())) ||
      rejected_conflict.toBool() ||
      !keyboard_shortcuts->property("shortcutEditorStatus")
           .toString()
           .contains(QStringLiteral("conflicts"))) {
    return fail("keyboard shortcuts dialog should detect and reject conflicting mappings");
  }
  if (keyboard_shortcuts->property("shortcutPresetCount").toInt() < 4) {
    return fail("keyboard shortcuts dialog should expose Lightroom, Photoshop, Capture One, and app-native presets");
  }
  const QString shortcut_presets[] = {
      QStringLiteral("lightroom"),
      QStringLiteral("photoshop"),
      QStringLiteral("capture-one"),
      QStringLiteral("app-native"),
  };
  for (const auto& preset_id : shortcut_presets) {
    QVariant applied_preset;
    QVariant conflict_count;
    if (!QMetaObject::invokeMethod(
            keyboard_shortcuts,
            "applyShortcutPreset",
            Q_RETURN_ARG(QVariant, applied_preset),
            Q_ARG(QVariant, preset_id)) ||
        !applied_preset.toBool() ||
        keyboard_shortcuts->property("activeShortcutPreset").toString() != preset_id ||
        !QMetaObject::invokeMethod(
            keyboard_shortcuts,
            "shortcutConflictCount",
            Q_RETURN_ARG(QVariant, conflict_count)) ||
        conflict_count.toInt() != 0) {
      return fail("keyboard shortcuts dialog should apply named shortcut presets without conflicts");
    }
  }
  keyboard_shortcuts->setProperty("shortcutSearchText", QStringLiteral("timeline"));
  QVariant timeline_shortcut_results;
  if (!QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "shortcutSearchResultCount",
          Q_RETURN_ARG(QVariant, timeline_shortcut_results)) ||
      timeline_shortcut_results.toInt() < 4) {
    return fail("keyboard shortcuts dialog should search shortcut descriptions and mappings");
  }
  keyboard_shortcuts->setProperty("shortcutSearchText", QStringLiteral("zzzz-no-shortcut"));
  QVariant empty_shortcut_results;
  if (!QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "shortcutSearchResultCount",
          Q_RETURN_ARG(QVariant, empty_shortcut_results)) ||
      empty_shortcut_results.toInt() != 0) {
    return fail("keyboard shortcuts dialog should report empty shortcut search results");
  }
  qunsetenv("PHOTO_EDITOR_DATABASE_URL");
  qunsetenv("PHOTO_EDITOR_CACHE_DIR");
  qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
  return EXIT_SUCCESS;
}
