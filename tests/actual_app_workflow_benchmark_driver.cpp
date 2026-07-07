#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <numeric>
#include <vector>

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QTime>
#include <QUrl>
#include <QVariant>
#include <QVector>
#include <qqml.h>

#include "app_shell_controller.h"
#include "brush_live_preview_item.h"
#include "carousel_document_controller.h"
#include "catalog_asset_model.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_provider.h"
#include "editor_preview_texture_item.h"
#include "export_job_controller.h"
#include "job_queue_model.h"
#include "job_runner.h"
#include "macos_updater.h"
#include "observability.h"
#include "preview_backend_controller.h"
#include "tablet_input_area.h"
#include "timeline_document_controller.h"
#include "timeline_playback_controller.h"

namespace photo_editor::desktop {

struct EditorPreviewControllerTestAccess {
  static int pending_brush_commit_count(const EditorPreviewController& controller) {
    return static_cast<int>(controller.pending_brush_commits_.size()) +
           (controller.pending_brush_commit_scheduled_ ? 1 : 0) +
           (controller.brush_raster_commit_watcher_.isRunning() ? 1 : 0) +
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

void process_layout_events(int rounds = 3) {
  for (int index = 0; index < rounds; ++index) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
}

bool wait_for_condition(const std::function<bool()>& condition, int timeout_ms = 3000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    if (condition()) {
      return true;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
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

bool named_item_visible(QObject* root, const QString& object_name) {
  return item_visible(find_named_item(root, object_name));
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
  if (window == nullptr || capture_dir.isEmpty() || captures == nullptr) {
    return;
  }
  if (!QDir().mkpath(capture_dir)) {
    return;
  }
  process_layout_events(8);
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

}  // namespace

int main(int argc, char** argv) {
  if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  }

  QApplication app(argc, argv);
  QQuickStyle::setStyle(QStringLiteral("Basic"));
  QApplication::setFont(QFont(QStringLiteral("Helvetica Neue")));
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorBenchmarks"));
  QCoreApplication::setApplicationName(QStringLiteral("ActualAppWorkflowBenchmark"));

  const auto arguments = app.arguments();
  const auto iterations = std::max(1, option_int(arguments, QStringLiteral("--iterations"), 5));
  const auto timeout_ms =
      std::max(1000, option_int(arguments, QStringLiteral("--timeout-ms"), 15000));
  const auto capture_dir = option_value(arguments, QStringLiteral("--capture-dir"));
  auto source_path = option_value(arguments, QStringLiteral("--source"));

  QTemporaryDir runtime_root;
  QTemporaryDir cache_root;
  QTemporaryDir fixture_dir;
  if (!runtime_root.isValid() || !cache_root.isValid() || !fixture_dir.isValid()) {
    return fail(QStringLiteral("Unable to create actual-app workflow temporary directories."));
  }
  if (!qEnvironmentVariableIsSet("PHOTO_EDITOR_APP_DATA_DIR")) {
    qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  }
  if (!qEnvironmentVariableIsSet("PHOTO_EDITOR_CACHE_DIR")) {
    qputenv("PHOTO_EDITOR_CACHE_DIR", cache_root.path().toUtf8());
  }
  qputenv("PHOTO_EDITOR_DATABASE_URL", QByteArray("postgres://example/test"));

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
          {QStringLiteral("elapsed_ms"), 0},
          {QStringLiteral("iterations"), iterations},
          {QStringLiteral("source"), source_path},
      });

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
  editor_preview.setPreviewTargetEdge(960);

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
  auto* editor_preview_image_provider = new photo_editor::desktop::EditorPreviewImageProvider;
  editor_preview.setImageProvider(editor_preview_image_provider);
  engine.addImageProvider(QStringLiteral("editor-preview"), editor_preview_image_provider);
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

  QElapsedTimer shell_timer;
  shell_timer.start();
  const auto main_qml_path =
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)).filePath(QStringLiteral("apps/desktop/qml/Main.qml"));
  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("benchmark.actual_app_workflow.qml_load_started"),
      {{QStringLiteral("workflow"), QStringLiteral("actual_app_qml")},
       {QStringLiteral("elapsed_ms"), 0},
       {QStringLiteral("path"), main_qml_path}});
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
  root->setProperty("width", 1600);
  root->setProperty("height", 1000);
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
  std::vector<double> brush_feedback_samples;
  std::vector<double> brush_commit_visible_samples;
  std::vector<double> tool_draft_samples;
  std::vector<double> carousel_route_samples;
  std::vector<double> timeline_route_samples;
  std::vector<double> export_route_samples;
  std::vector<double> frame_tick_samples;

  edit_prewarm_samples.reserve(1);
  library_samples.reserve(static_cast<std::size_t>(iterations));
  import_samples.reserve(static_cast<std::size_t>(iterations));
  open_samples.reserve(static_cast<std::size_t>(iterations));
  masks_samples.reserve(static_cast<std::size_t>(iterations));
  brush_page_samples.reserve(static_cast<std::size_t>(iterations));
  brush_feedback_samples.reserve(static_cast<std::size_t>(iterations));
  brush_commit_visible_samples.reserve(static_cast<std::size_t>(iterations));
  tool_draft_samples.reserve(static_cast<std::size_t>(iterations));
  carousel_route_samples.reserve(static_cast<std::size_t>(iterations));
  timeline_route_samples.reserve(static_cast<std::size_t>(iterations));
  export_route_samples.reserve(static_cast<std::size_t>(iterations));
  frame_tick_samples.reserve(static_cast<std::size_t>(iterations));

  QVector<photo_editor::desktop::CatalogAssetItem> workflow_assets;
  workflow_assets.reserve(iterations);
  for (int iteration = 0; iteration < iterations; ++iteration) {
    workflow_assets.append(make_asset(
        QStringLiteral("actual-app-workflow-%1").arg(iteration + 1),
        source_path));
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
         {QStringLiteral("source_image_width"), editor_preview.sourceImageWidth()},
         {QStringLiteral("source_image_height"), editor_preview.sourceImageHeight()}});
    if (iteration == 0) {
      save_capture_if_requested(
          root_window,
          capture_dir,
          QStringLiteral("03-edit-open-photo"),
          &captures);
    }

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
                     named_item_visible(workspace, QStringLiteral("maskSourceBrushAddButton"));
            },
            timeout_ms)) {
      return fail(QStringLiteral("Timed out opening the Masks page tool rail."));
    }
    masks_samples.push_back(static_cast<double>(masks_timer.elapsed()));
    log_stage(QStringLiteral("masks_page_to_tool_rail_ready"), masks_timer.elapsed(), iteration);

    QElapsedTimer brush_page_timer;
    brush_page_timer.start();
    auto* edit_workspace = active_workspace(root);
    qint64 brush_click_invoke_ms = -1;
    qint64 brush_click_layout_ms = -1;
    auto* brush_button = find_named_object(edit_workspace, QStringLiteral("maskSourceBrushAddButton"));
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
                     context_ready &&
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

    editor_preview.beginActiveBrushStroke();
    QElapsedTimer feedback_timer;
    feedback_timer.start();
    editor_preview.addBrushPoint(0.31, 0.36);
    if (!wait_for_condition(
            [&]() {
              return !editor_preview.activeMaskLiveBrushPreviewBackend().isEmpty() &&
                     editor_preview.activeMaskLiveBrushPreviewElapsedMs() > 0;
            },
            timeout_ms)) {
      return fail(QStringLiteral("Timed out waiting for brush live feedback."));
    }
    brush_feedback_samples.push_back(static_cast<double>(feedback_timer.elapsed()));
    log_stage(
        QStringLiteral("brush_pointer_down_to_qml_feedback"),
        feedback_timer.elapsed(),
        iteration,
        {{QStringLiteral("live_preview_elapsed_ms"),
          editor_preview.activeMaskLiveBrushPreviewElapsedMs()},
         {QStringLiteral("live_preview_backend"),
          editor_preview.activeMaskLiveBrushPreviewBackend()},
         {QStringLiteral("live_preview_within_latency_budget"),
          editor_preview.activeMaskLiveBrushPreviewWithinLatencyBudget()}});
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

    QElapsedTimer carousel_route_timer;
    carousel_route_timer.start();
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
    carousel_route_samples.push_back(static_cast<double>(carousel_route_timer.elapsed()));
    log_stage(
        QStringLiteral("carousel_route_to_visible"),
        carousel_route_timer.elapsed(),
        iteration);

    QElapsedTimer timeline_route_timer;
    timeline_route_timer.start();
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
    timeline_route_samples.push_back(static_cast<double>(timeline_route_timer.elapsed()));
    log_stage(
        QStringLiteral("timeline_route_to_visible"),
        timeline_route_timer.elapsed(),
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
      QStringLiteral("brush_pointer_down_to_qml_feedback"),
      summarize_samples(brush_feedback_samples),
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
      iterations);
  log_summary(
      QStringLiteral("timeline_route_to_visible"),
      summarize_samples(timeline_route_samples),
      iterations);
  log_summary(
      QStringLiteral("export_route_to_visible"),
      summarize_samples(export_route_samples),
      iterations);
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
          {QStringLiteral("timeline_route_p95_ms"),
           summarize_samples(timeline_route_samples).p95_ms},
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
