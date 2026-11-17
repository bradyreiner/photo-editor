#include "search_model_paths.h"
#include <QApplication>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QJSValue>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPointer>
#include <QScopeGuard>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImageReader>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QSize>
#include <QSettings>
#include <QSet>
#include <QString>
#include <QTextStream>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>

#include <functional>
#include <memory>
#include "managed_agent_runtime.h"
#include "agent_task_recipe_persistence.h"
#include "gaussian_scene_controller.h"
#include "gaussian_interaction_proof.h"
#include "gaussian_capture_preparation.h"
#include "gaussian_video_capture.h"
#include "gaussian_scene_viewport.h"
#include "agent_runtime_paths.h"
#include "agent_execution_admission.h"

#include "app_bootstrap.h"
#include "catalog_readiness_probe.h"
#include "app_shell_controller.h"
#include "customer_access_controller.h"
#include "crash_reporting_controller.h"
#include "carousel_document_controller.h"
#include "catalog_asset_model.h"
#include "library_workflow_controller.h"
#include "library_original_image_provider.h"
#include "import_preview_image_provider.h"
#include "visual_search_controller.h"
#include "catalog_similarity_search.h"
#include "search_runtime_bundle.h"
#include "search_model_setup_controller.h"
#include "gaussian_spacetime_artifact.h"
#include "search_edit_workflow.h"
#include "editor_preview_controller.h"
#include "photo_component_workspace_controller.h"
#include "edit_adjustment_patch.h"
#include "editor_close_coordinator.h"
#include <QSaveFile>
#include "editor_preview_image_provider.h"
#include "creative_example_thumbnail_image_provider.h"
#include "export_job_controller.h"
#include "job_queue_model.h"
#include "job_runner.h"
#include "job_store.h"
#include "macos_updater.h"
#include "observability.h"
#include "agent_edit_controller.h"
#include "agent_session_controller.h"
#include "agent_photo_commands.h"
#include "agent_task_coordinator.h"
#include "agent_task_permissions.h"
#include "agent_prepared_canvas_task.h"
#include "agent_timeline_commands.h"
#include "agent_timeline_compound_commands.h"
#include "ai_model_runtime.h"
#include "photo_automation_bridge.h"
#include "preview_backend_controller.h"
#include "qml_performance_monitor.h"
#include "rendering_migration_batch_command.h"
#include "timeline_document_controller.h"
#include "timeline_color_edit_controller.h"
#include "timeline_draft_lifecycle.h"
#include "timeline_playback_controller.h"
#include "timeline_fractal_noise_source.h"
#include "timeline_route_performance_lifecycle_probe.h"
#include "recent_user_input_filter.h"
#include "creative_fonts.h"
#include "../../../tests/agent_full_workflow_runtime_proof.h"

namespace {

constexpr auto kRenderingMigrationReportArgument =
    "--rendering-migration-report";
constexpr auto kRenderingMigrationRecipeAssetsArgument =
    "--rendering-migration-recipe-assets";
constexpr auto kRenderingMigrationMergeReportArgument =
    "--merge-rendering-migration-report";
constexpr auto kRenderingMigrationComponentReportArgument =
    "--rendering-migration-component-report";
constexpr auto kRenderingMigrationComponentReceiptArgument =
    "--rendering-migration-component-receipt";
constexpr auto kRenderingMigrationPlanOnlyArgument =
    "--rendering-migration-plan-only";
constexpr auto kRenderingMigrationChunkAuthorizationArgument =
    "--rendering-migration-chunk-authorization";
constexpr auto kEnableAutomationArgument = "--enable-automation";

[[nodiscard]] QString canonicalOutputFilePath(const QString& path) {
  const QFileInfo input(path);
  if (path.trimmed().isEmpty() || !input.isAbsolute() || input.fileName().isEmpty()) return {};
  const auto parent = input.dir().canonicalPath();
  if (parent.isEmpty()) return {};
  return QDir(parent).filePath(input.fileName());
}

[[nodiscard]] bool pathIsWithinDirectory(const QString& directory, const QString& path) {
  const auto root = QDir::cleanPath(directory);
  const auto candidate = QDir::cleanPath(path);
  if (root.isEmpty() || candidate.isEmpty() || candidate == root) return false;
  const auto relative = QDir(root).relativeFilePath(candidate);
  return !relative.isEmpty() && relative != QStringLiteral(".") &&
      relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../")) &&
      !QFileInfo(relative).isAbsolute();
}

[[nodiscard]] bool automationRequested(const QStringList& arguments) {
  if (arguments.contains(QString::fromLatin1(kEnableAutomationArgument))) {
    return true;
  }
  const auto policy = qEnvironmentVariable("PHOTO_EDITOR_ENABLE_AUTOMATION")
                          .trimmed()
                          .toLower();
  return policy == QStringLiteral("1") ||
         policy == QStringLiteral("true") ||
         policy == QStringLiteral("yes") ||
         policy == QStringLiteral("on");
}

[[nodiscard]] QStringList requestedAutomationCapabilities() {
  const auto configured =
      qEnvironmentVariable("PHOTO_EDITOR_AUTOMATION_CAPABILITIES").trimmed();
  if (configured.isEmpty()) {
    return {QStringLiteral("read")};
  }
  return configured.split(QLatin1Char(','), Qt::SkipEmptyParts);
}

[[nodiscard]] bool renderingMigrationCommandRequested(
    int argc,
    char* argv[]) {
  for (auto index = 1; index < argc; ++index) {
    if (QString::fromLocal8Bit(argv[index]) ==
        QString::fromLatin1(
            kRenderingMigrationReportArgument) ||
        QString::fromLocal8Bit(argv[index]) ==
            QString::fromLatin1(
                kRenderingMigrationMergeReportArgument)) {
      return true;
    }
  }
  return false;
}

int runRenderingMigrationCommand(
    const QStringList& arguments) {
  QString report_path;
  QStringList component_report_paths;
  QStringList component_receipt_paths;
  auto merge_requested = false;
  auto recipe_assets_requested = false;
  auto plan_only = false;
  QString chunk_authorization_path;
  auto recipe_asset_root =
      QDir(
          photo_editor::desktop::AppBootstrap::appDataLocation())
          .filePath(QStringLiteral("recipe-assets"));
  for (auto index = 1; index < arguments.size(); ++index) {
    const auto& argument = arguments.at(index);
    if (argument ==
        QString::fromLatin1(
            kRenderingMigrationReportArgument)) {
      if (merge_requested) {
        QTextStream(stderr)
            << "batch and merge migration commands cannot be combined\n";
        return EXIT_FAILURE;
      }
      if (++index >= arguments.size()) {
        QTextStream(stderr)
            << "missing value for "
            << kRenderingMigrationReportArgument << '\n';
        return EXIT_FAILURE;
      }
      report_path = arguments.at(index);
      continue;
    }
    if (argument ==
        QString::fromLatin1(
            kRenderingMigrationMergeReportArgument)) {
      if (!report_path.isEmpty()) {
        QTextStream(stderr)
            << "batch and merge migration commands cannot be combined\n";
        return EXIT_FAILURE;
      }
      if (++index >= arguments.size()) {
        QTextStream(stderr)
            << "missing value for "
            << kRenderingMigrationMergeReportArgument << '\n';
        return EXIT_FAILURE;
      }
      merge_requested = true;
      report_path = arguments.at(index);
      continue;
    }
    if (argument ==
        QString::fromLatin1(
            kRenderingMigrationComponentReportArgument)) {
      if (++index >= arguments.size()) {
        QTextStream(stderr)
            << "missing value for "
            << kRenderingMigrationComponentReportArgument << '\n';
        return EXIT_FAILURE;
      }
      component_report_paths.append(arguments.at(index));
      continue;
    }
    if (argument ==
        QString::fromLatin1(
            kRenderingMigrationComponentReceiptArgument)) {
      if (++index >= arguments.size()) {
        QTextStream(stderr)
            << "missing value for "
            << kRenderingMigrationComponentReceiptArgument << '\n';
        return EXIT_FAILURE;
      }
      component_receipt_paths.append(arguments.at(index));
      continue;
    }
    if (argument ==
        QString::fromLatin1(
            kRenderingMigrationPlanOnlyArgument)) {
      plan_only = true;
      continue;
    }
    if (argument ==
        QString::fromLatin1(
            kRenderingMigrationChunkAuthorizationArgument)) {
      if (++index >= arguments.size()) {
        QTextStream(stderr)
            << "missing value for "
            << kRenderingMigrationChunkAuthorizationArgument << '\n';
        return EXIT_FAILURE;
      }
      if (!chunk_authorization_path.isEmpty()) {
        QTextStream(stderr)
            << "rendering migration accepts only one chunk authorization\n";
        return EXIT_FAILURE;
      }
      chunk_authorization_path = arguments.at(index);
      continue;
    }
    if (argument ==
        QString::fromLatin1(
            kRenderingMigrationRecipeAssetsArgument)) {
      if (++index >= arguments.size()) {
        QTextStream(stderr)
            << "missing value for "
            << kRenderingMigrationRecipeAssetsArgument << '\n';
        return EXIT_FAILURE;
      }
      recipe_assets_requested = true;
      recipe_asset_root = arguments.at(index);
      continue;
    }
    QTextStream(stderr)
        << "unknown rendering migration argument: "
        << argument << '\n';
    return EXIT_FAILURE;
  }

  if (merge_requested && recipe_assets_requested) {
    QTextStream(stderr)
        << "merge migration command does not accept recipe assets\n";
    return EXIT_FAILURE;
  }
  if (merge_requested && plan_only) {
    QTextStream(stderr)
        << "merge migration command does not accept plan-only mode\n";
    return EXIT_FAILURE;
  }
  if (merge_requested && !chunk_authorization_path.isEmpty()) {
    QTextStream(stderr)
        << "merge migration command does not accept a chunk authorization\n";
    return EXIT_FAILURE;
  }
  if (!merge_requested && !component_report_paths.isEmpty()) {
    QTextStream(stderr)
        << "component reports require the merge migration command\n";
    return EXIT_FAILURE;
  }
  if (!merge_requested && !component_receipt_paths.isEmpty()) {
    QTextStream(stderr)
        << "component receipts require the merge migration command\n";
    return EXIT_FAILURE;
  }
  const auto command_result = merge_requested
      ? photo_editor::desktop::
            run_packaged_rendering_migration_report_merge_command(
                report_path,
                component_report_paths,
                component_receipt_paths)
      : photo_editor::desktop::
            run_packaged_rendering_migration_batch_command(
                report_path,
                recipe_asset_root,
                120000,
                plan_only,
                chunk_authorization_path);
  if (!command_result.valid) {
    QTextStream(stderr)
        << "rendering migration command failed: "
        << command_result.error << '\n';
    return EXIT_FAILURE;
  }
  const QJsonObject output{
      {QStringLiteral("schema"),
       QStringLiteral(
           "photo-editor-rendering-migration-command-result:v1")},
      {QStringLiteral("reportPath"),
       QFileInfo(report_path).absoluteFilePath()},
      {QStringLiteral("reportSha256"),
       command_result.report.report_hash},
      {QStringLiteral("canonicalReportPayloadSha256"),
       command_result.report.report_hash},
      {QStringLiteral("reportFileSha256"),
       command_result.report_file_sha256},
      {QStringLiteral("evidenceArtifactRoot"),
       command_result.evidence_artifact_root},
      {QStringLiteral("summary"),
       QJsonObject::fromVariantMap(
           command_result.report.document
               .value(QStringLiteral("summary"))
               .toMap())},
  };
  QTextStream(stdout)
      << QJsonDocument(output).toJson(
             QJsonDocument::Indented);
  return EXIT_SUCCESS;
}

void logProofAutorunEvent(
    const QString& event,
    const QString& source_path,
    photo_editor::desktop::EditorPreviewController* editor_preview) {
  const auto native_size = editor_preview->displayPreviewNativeSize();
  photo_editor::desktop::observability::logInfo(
      photoEditorAppLog(),
      event,
      {
          {QStringLiteral("file_path"), source_path},
          {QStringLiteral("processing"), editor_preview->processing()},
          {QStringLiteral("native_available"), editor_preview->displayPreviewNativeAvailable()},
          {QStringLiteral("native_width"), native_size.width()},
          {QStringLiteral("native_height"), native_size.height()},
          {QStringLiteral("native_revision"), editor_preview->displayPreviewNativeRevision()},
          {QStringLiteral("source_width"), editor_preview->sourceImageWidth()},
          {QStringLiteral("source_height"), editor_preview->sourceImageHeight()},
      });
}

void waitForNativeMaxPreview(
    photo_editor::desktop::EditorPreviewController* editor_preview,
    const QString& source_path,
    const QString& label,
    int min_native_revision,
    int attempts_remaining,
    std::function<void()> on_ready) {
  const auto native_size = editor_preview->displayPreviewNativeSize();
  const QSize source_size(editor_preview->sourceImageWidth(), editor_preview->sourceImageHeight());
  const auto ready = editor_preview->displayPreviewNativeAvailable() &&
                     source_size.isValid() &&
                     native_size == source_size &&
                     editor_preview->displayPreviewNativeRevision() > min_native_revision &&
                     !editor_preview->processing();
  if (ready) {
    logProofAutorunEvent(
        QStringLiteral("desktop.proof_autorun.%1_ready").arg(label),
        source_path,
        editor_preview);
    on_ready();
    return;
  }

  if (attempts_remaining <= 0) {
    photo_editor::desktop::observability::logWarning(
        photoEditorAppLog(),
        QStringLiteral("desktop.proof_autorun.%1_timeout").arg(label),
        {
            {QStringLiteral("file_path"), source_path},
            {QStringLiteral("processing"), editor_preview->processing()},
            {QStringLiteral("native_available"), editor_preview->displayPreviewNativeAvailable()},
            {QStringLiteral("native_width"), native_size.width()},
            {QStringLiteral("native_height"), native_size.height()},
            {QStringLiteral("native_revision"), editor_preview->displayPreviewNativeRevision()},
            {QStringLiteral("min_native_revision"), min_native_revision},
            {QStringLiteral("source_width"), source_size.width()},
            {QStringLiteral("source_height"), source_size.height()},
        });
    QTimer::singleShot(1000, qApp, []() { QCoreApplication::quit(); });
    return;
  }

  if (attempts_remaining % 10 == 0) {
    logProofAutorunEvent(
        QStringLiteral("desktop.proof_autorun.%1_waiting").arg(label),
        source_path,
        editor_preview);
  }

  QTimer::singleShot(
      500,
      editor_preview,
      [editor_preview,
       source_path,
       label,
       min_native_revision,
       attempts_remaining,
       on_ready = std::move(on_ready)]() mutable {
        waitForNativeMaxPreview(
            editor_preview,
            source_path,
            label,
            min_native_revision,
            attempts_remaining - 1,
            std::move(on_ready));
      });
}

void waitForLoadedSource(
    photo_editor::desktop::EditorPreviewController* editor_preview,
    const QString& source_path,
    int attempts_remaining,
    std::function<void()> on_ready) {
  const QSize source_size(editor_preview->sourceImageWidth(), editor_preview->sourceImageHeight());
  const auto ready = editor_preview->hasAsset() && source_size.isValid() &&
                     source_size.width() >= 2048 && source_size.height() >= 2048 &&
                     !editor_preview->processing();
  if (ready) {
    logProofAutorunEvent(
        QStringLiteral("desktop.proof_autorun.source_ready"),
        source_path,
        editor_preview);
    on_ready();
    return;
  }

  if (attempts_remaining <= 0) {
    photo_editor::desktop::observability::logWarning(
        photoEditorAppLog(),
        QStringLiteral("desktop.proof_autorun.source_timeout"),
        {
            {QStringLiteral("file_path"), source_path},
            {QStringLiteral("has_asset"), editor_preview->hasAsset()},
            {QStringLiteral("processing"), editor_preview->processing()},
            {QStringLiteral("source_width"), source_size.width()},
            {QStringLiteral("source_height"), source_size.height()},
        });
    QTimer::singleShot(1000, qApp, []() { QCoreApplication::quit(); });
    return;
  }

  if (attempts_remaining % 10 == 0) {
    logProofAutorunEvent(
        QStringLiteral("desktop.proof_autorun.source_waiting"),
        source_path,
        editor_preview);
  }

  QTimer::singleShot(
      500,
      editor_preview,
      [editor_preview,
       source_path,
       attempts_remaining,
       on_ready = std::move(on_ready)]() mutable {
        waitForLoadedSource(
            editor_preview,
            source_path,
            attempts_remaining - 1,
            std::move(on_ready));
      });
}

void waitForFullRenderAndApplyTint(
    photo_editor::desktop::EditorPreviewController* editor_preview,
    const QString& source_path,
    double proof_tint,
    int attempts_remaining) {
  if (editor_preview->fullRenderRunning()) {
    photo_editor::desktop::observability::logInfo(
        photoEditorAppLog(),
        QStringLiteral("desktop.proof_autorun.tint_followup_edit"),
        {{QStringLiteral("file_path"), source_path},
         {QStringLiteral("attempts_remaining"), attempts_remaining}});
    editor_preview->setTint(proof_tint);
    return;
  }

  if (attempts_remaining <= 0) {
    photo_editor::desktop::observability::logWarning(
        photoEditorAppLog(),
        QStringLiteral("desktop.proof_autorun.tint_followup_timeout"),
        {{QStringLiteral("file_path"), source_path}});
    return;
  }

  QTimer::singleShot(10, editor_preview, [editor_preview, source_path, proof_tint, attempts_remaining]() {
    waitForFullRenderAndApplyTint(editor_preview, source_path, proof_tint, attempts_remaining - 1);
  });
}

void scheduleViewportProofEdits(
    photo_editor::desktop::EditorPreviewController* editor_preview,
    const QString& source_path,
    double proof_temperature,
    double proof_tint) {
  photo_editor::desktop::observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("desktop.proof_autorun.viewport_first"),
      {{QStringLiteral("file_path"), source_path}});
  editor_preview->setPreviewViewport(0.75, 0.75, 0.125, 0.125, 512, true);

  QTimer::singleShot(900, editor_preview, [editor_preview, source_path, proof_temperature]() {
    photo_editor::desktop::observability::logInfo(
        photoEditorAppLog(),
        QStringLiteral("desktop.proof_autorun.temperature_edit"),
        {{QStringLiteral("file_path"), source_path}});
    editor_preview->setTemperature(proof_temperature);
  });

  QTimer::singleShot(1200, editor_preview, [editor_preview, source_path]() {
    photo_editor::desktop::observability::logInfo(
        photoEditorAppLog(),
        QStringLiteral("desktop.proof_autorun.viewport_second"),
        {{QStringLiteral("file_path"), source_path}});
    editor_preview->setPreviewViewport(0.625, 0.625, 0.125, 0.125, 512, true);
  });

  QTimer::singleShot(1210, editor_preview, [editor_preview, source_path, proof_tint]() {
    waitForFullRenderAndApplyTint(editor_preview, source_path, proof_tint, 300);
  });

  QTimer::singleShot(30000, qApp, []() {
    photo_editor::desktop::observability::logInfo(
        photoEditorAppLog(),
        QStringLiteral("desktop.proof_autorun.quit"),
        {});
    QCoreApplication::quit();
  });
}

void scheduleDesktopProofAutorun(photo_editor::desktop::EditorPreviewController* editor_preview) {
  const auto source_path = qEnvironmentVariable("PHOTO_EDITOR_DESKTOP_PROOF_SOURCE_IMAGE").trimmed();
  if (source_path.isEmpty()) {
    return;
  }

  const QFileInfo source_info(source_path);
  if (!source_info.isFile()) {
    photo_editor::desktop::observability::logWarning(
        photoEditorAppLog(),
        QStringLiteral("desktop.proof_autorun.source_missing"),
        {{QStringLiteral("file_path"), source_path}});
    return;
  }

  const auto normalized_source_path = source_info.absoluteFilePath();
  const auto display_name = source_info.completeBaseName();
  photo_editor::desktop::observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("desktop.proof_autorun.scheduled"),
      {{QStringLiteral("file_path"), normalized_source_path}});

  QTimer::singleShot(500, editor_preview, [editor_preview, normalized_source_path, display_name]() {
    photo_editor::desktop::observability::logInfo(
        photoEditorAppLog(),
        QStringLiteral("desktop.proof_autorun.load_asset"),
        {{QStringLiteral("file_path"), normalized_source_path}});
    editor_preview->loadAsset(
        QStringLiteral("desktop-proof-source"),
        normalized_source_path,
        QString{},
        display_name,
        QString{},
        QString{},
        QString{});
  });

  waitForLoadedSource(
      editor_preview,
      normalized_source_path,
      120,
      [editor_preview, normalized_source_path]() {
        const auto revision_before_texture = editor_preview->displayPreviewNativeRevision();
        const auto proof_texture =
            10.0 +
            static_cast<double>(QDateTime::currentMSecsSinceEpoch() % 3000) / 100.0;
        photo_editor::desktop::observability::logInfo(
            photoEditorAppLog(),
            QStringLiteral("desktop.proof_autorun.working_profile"),
            {{QStringLiteral("file_path"), normalized_source_path},
             {QStringLiteral("working_profile"), QStringLiteral("Linear sRGB")}});
        editor_preview->setWorkingColorProfile(QStringLiteral("Linear sRGB"));
        photo_editor::desktop::observability::logInfo(
            photoEditorAppLog(),
            QStringLiteral("desktop.proof_autorun.texture_edit"),
            {{QStringLiteral("file_path"), normalized_source_path},
             {QStringLiteral("native_revision"), revision_before_texture},
             {QStringLiteral("texture"), proof_texture}});
        editor_preview->setTexture(proof_texture);
        waitForNativeMaxPreview(
            editor_preview,
            normalized_source_path,
            QStringLiteral("adjusted_max_preview"),
            revision_before_texture,
            120,
            [editor_preview, normalized_source_path]() {
              const auto proof_temperature =
                  10.0 +
                  static_cast<double>(QDateTime::currentMSecsSinceEpoch() % 3000) / 100.0;
              const auto proof_tint =
                  15.0 +
                  static_cast<double>(QDateTime::currentMSecsSinceEpoch() % 2500) / 100.0;
              scheduleViewportProofEdits(
                  editor_preview,
                  normalized_source_path,
                  proof_temperature,
                  proof_tint);
            });
      });
}

}  // namespace

int main(int argc, char* argv[]) {
  if (renderingMigrationCommandRequested(
          argc, argv)) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(
        photo_editor::desktop::AppBootstrap::application_name);
    QCoreApplication::setOrganizationName(
        photo_editor::desktop::AppBootstrap::organization_name);
    if (!photo_editor::desktop::AppBootstrap::
            ensureRuntimeDirectories()) {
      QTextStream(stderr)
          << "rendering migration batch failed: runtime directories are unavailable\n";
      return EXIT_FAILURE;
    }
    return runRenderingMigrationCommand(
        QCoreApplication::arguments());
  }
  QElapsedTimer startup_timer;
  startup_timer.start();
  // Continuous loading feedback uses Qt Quick's scene graph animators. Keep
  // their clock independent of GUI work; preserve explicit diagnostic overrides.
  if (qEnvironmentVariableIsEmpty("QSG_RENDER_LOOP"))
    qputenv("QSG_RENDER_LOOP", QByteArrayLiteral("threaded"));
  QQuickStyle::setStyle(QStringLiteral("Basic"));
  QApplication app(argc, argv);
  const auto font_failures = photo_editor::desktop::register_creative_fonts();
  for (const auto& path : font_failures)
    qWarning() << "Could not load bundled creative font:" << path;
  QApplication::setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
  QApplication::setApplicationName(photo_editor::desktop::AppBootstrap::application_name);
  QApplication::setOrganizationName(photo_editor::desktop::AppBootstrap::organization_name);
  // Isolated desktop workers must not overwrite the regular app's workspace
  // layout and preferences while reviewing the same native editor surface.
  const auto preferences_directory =
      qEnvironmentVariable("PHOTO_EDITOR_PREFERENCES_DIR").trimmed();
  if (!preferences_directory.isEmpty()) {
    const QFileInfo preferences_info(preferences_directory);
    if (!preferences_info.isAbsolute() || preferences_info.isSymLink() ||
        !QDir().mkpath(preferences_directory) ||
        !QFileInfo(preferences_directory).isDir() ||
        !QFileInfo(preferences_directory).isWritable()) {
      qCritical() << "PHOTO_EDITOR_PREFERENCES_DIR must be an absolute writable directory.";
      return EXIT_FAILURE;
    }
    const auto private_preferences = QDir(preferences_directory).canonicalPath();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, private_preferences);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, private_preferences);
  }
  const auto lifecycle_flag =
      QStringLiteral("--timeline-route-performance-lifecycle");
  const auto source_flag = QStringLiteral("--timeline-route-performance-source");
  const auto image_flag = QStringLiteral("--timeline-route-performance-image");
  const bool timeline_route_performance_lifecycle =
      app.arguments().contains(lifecycle_flag);
  const bool legacy_timeline_route_performance =
      app.arguments().contains(QStringLiteral("--timeline-route-performance"));
  QString lifecycle_output_directory;
  QString lifecycle_source_video;
  QString lifecycle_source_image;
  if (timeline_route_performance_lifecycle) {
    QStringList validation_errors;
    const auto argument_value = [&app, &validation_errors](const QString& name,
                                                           bool required) {
      const auto index = app.arguments().indexOf(name);
      if (index < 0) {
        if (required) {
          validation_errors.push_back(QStringLiteral("Missing %1 argument.").arg(name));
        }
        return QString{};
      }
      if (app.arguments().count(name) != 1 || index + 1 >= app.arguments().size() ||
          app.arguments().at(index + 1).startsWith(QStringLiteral("--"))) {
        validation_errors.push_back(QStringLiteral("Invalid value for %1.").arg(name));
        return QString{};
      }
      return app.arguments().at(index + 1);
    };
    lifecycle_output_directory = argument_value(lifecycle_flag, true);
    lifecycle_source_video = argument_value(source_flag, true);
    lifecycle_source_image = argument_value(image_flag, false);
    if (legacy_timeline_route_performance) {
      validation_errors.push_back(QStringLiteral(
          "The legacy one-shot route probe cannot run together with lifecycle mode."));
    }
    if (app.arguments().contains(QStringLiteral("--managed-runtime-proof-dir")) ||
        app.arguments().contains(QStringLiteral("--agent-full-workflow-proof-dir")) ||
        app.arguments().contains(QStringLiteral("--timeline-route-performance-empty")) ||
        app.arguments().contains(QStringLiteral("--timeline-route-performance-creator-setup"))) {
      validation_errors.push_back(QStringLiteral(
          "Lifecycle mode cannot be combined with another runtime proof or an empty Timeline fixture."));
    }

    const QFileInfo output_info(lifecycle_output_directory);
    if (lifecycle_output_directory.isEmpty() || !output_info.isAbsolute() ||
        output_info.isSymLink()) {
      validation_errors.push_back(QStringLiteral(
          "The lifecycle output directory must be an absolute, non-symlink path."));
    } else if (QFileInfo::exists(QDir(lifecycle_output_directory).filePath(
                   QStringLiteral("lifecycle-receipt.json")))) {
      validation_errors.push_back(QStringLiteral(
          "The lifecycle output directory already contains a receipt."));
    }

    const QFileInfo video_info(lifecycle_source_video);
    if (lifecycle_source_video.isEmpty() || !video_info.isFile() ||
        video_info.size() <= 0 ||
        !video_info.isReadable() || video_info.canonicalFilePath().isEmpty()) {
      validation_errors.push_back(QStringLiteral(
          "The lifecycle video source must be a readable local file."));
    } else {
      lifecycle_source_video = video_info.canonicalFilePath();
    }
    if (!lifecycle_source_image.isEmpty()) {
      const QFileInfo image_info(lifecycle_source_image);
      if (!image_info.isFile() || image_info.size() <= 0 || !image_info.isReadable() ||
          image_info.canonicalFilePath().isEmpty()) {
        validation_errors.push_back(QStringLiteral(
            "The optional lifecycle image source must be a readable local file."));
      } else {
        lifecycle_source_image = image_info.canonicalFilePath();
        QImageReader image_reader(lifecycle_source_image);
        if (!image_reader.canRead()) {
          validation_errors.push_back(QStringLiteral(
              "The optional lifecycle image source is not a decodable image."));
        }
      }
    }

    const auto app_data_root = qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_DIR").trimmed();
    const auto cache_root = qEnvironmentVariable("PHOTO_EDITOR_CACHE_DIR").trimmed();
    const auto validate_runtime_root = [&validation_errors](
                                          const QString& name,
                                          const QString& path) {
      const QFileInfo root_info(path);
      if (path.isEmpty() || !root_info.isAbsolute() || root_info.isSymLink()) {
        validation_errors.push_back(QStringLiteral(
            "%1 must name a caller-supplied absolute, non-symlink directory.").arg(name));
      }
    };
    validate_runtime_root(QStringLiteral("PHOTO_EDITOR_APP_DATA_DIR"), app_data_root);
    validate_runtime_root(QStringLiteral("PHOTO_EDITOR_CACHE_DIR"), cache_root);
    const auto database_url = qEnvironmentVariable("PHOTO_EDITOR_DATABASE_URL").trimmed();
    const QUrl database(database_url);
    const auto database_name = database.path().section(QLatin1Char('/'), -1);
    const auto local_database =
        database.isValid() &&
        QStringList{QStringLiteral("postgres"), QStringLiteral("postgresql")}
            .contains(database.scheme(), Qt::CaseInsensitive) &&
        database.host() == QStringLiteral("127.0.0.1") && database.port() > 0 &&
        database_name.contains(QStringLiteral("test"), Qt::CaseInsensitive);
    if (!local_database) {
      validation_errors.push_back(QStringLiteral(
          "PHOTO_EDITOR_DATABASE_URL must point to a local PostgreSQL test database."));
    }

    if (!validation_errors.isEmpty()) {
      QVariantMap inputs{
          {QStringLiteral("outputDirectory"), lifecycle_output_directory},
          {QStringLiteral("sourceVideoPath"), lifecycle_source_video},
          {QStringLiteral("sourceImagePath"), lifecycle_source_image},
          {QStringLiteral("appDataRoot"), app_data_root},
          {QStringLiteral("cacheRoot"), cache_root},
          {QStringLiteral("databaseHost"), database.host()},
          {QStringLiteral("databaseName"), database_name},
          {QStringLiteral("databasePort"), database.port()},
      };
      photo_editor::desktop::TimelineRoutePerformanceLifecycleProbe::
          writeCommandFailureReceipt(
              lifecycle_output_directory, validation_errors, inputs);
      qWarning() << "Timeline route lifecycle probe preflight failed:" << validation_errors;
      return EXIT_FAILURE;
    }
  } else if (app.arguments().contains(source_flag) ||
             app.arguments().contains(image_flag)) {
    qWarning() << source_flag << "and" << image_flag
               << "require" << lifecycle_flag;
    return EXIT_FAILURE;
  }

  QString agent_workflow_proof_dir;
  const auto agent_workflow_argument =
      app.arguments().indexOf(QStringLiteral("--agent-full-workflow-proof-dir"));
  if (agent_workflow_argument >= 0) {
    // This opt-in acceptance route uses the ordinary app wiring, but only a
    // caller-created, marked private runtime and its explicitly named test DB.
    // Set settings and runtime paths before constructing any app controller.
    if (agent_workflow_argument + 1 >= app.arguments().size() ||
        app.arguments().contains(QStringLiteral("--managed-runtime-proof-dir")) ||
        app.arguments().contains(QStringLiteral("--timeline-route-performance")) ||
        timeline_route_performance_lifecycle)
      return EXIT_FAILURE;
    const QFileInfo supplied_root(app.arguments().at(agent_workflow_argument + 1));
    if (!supplied_root.isAbsolute() || !supplied_root.isDir() || supplied_root.isSymLink())
      return EXIT_FAILURE;
    agent_workflow_proof_dir = supplied_root.canonicalFilePath();
    const QFileInfo marker_info(QDir(agent_workflow_proof_dir)
        .filePath(QStringLiteral("agent-workflow-private-runtime.json")));
    if (!marker_info.isFile() || marker_info.isSymLink()) return EXIT_FAILURE;
    QFile marker(marker_info.absoluteFilePath());
    if (!marker.open(QIODevice::ReadOnly)) return EXIT_FAILURE;
    const auto contract = QJsonDocument::fromJson(marker.readAll()).object();
    const auto database_url =
        qEnvironmentVariable("PHOTO_EDITOR_AGENT_WORKFLOW_TEST_DATABASE_URL").trimmed();
    const QUrl database(database_url);
    if (contract.value(QStringLiteral("schema")) !=
            QStringLiteral("agent-workflow-private-runtime:v1") ||
        contract.value(QStringLiteral("runtimeRoot")).toString() != agent_workflow_proof_dir ||
        contract.value(QStringLiteral("databaseUrl")).toString() != database_url ||
        !database.isValid() ||
        !QStringList{QStringLiteral("postgres"), QStringLiteral("postgresql")}
             .contains(database.scheme(), Qt::CaseInsensitive) ||
        database.host() != QStringLiteral("127.0.0.1") || database.port() <= 0 ||
        !database.path().contains(QStringLiteral("test"), Qt::CaseInsensitive) ||
        QFileInfo::exists(QDir(agent_workflow_proof_dir).filePath(QStringLiteral("workflow-receipt.json"))))
      return EXIT_FAILURE;
    qputenv("PHOTO_EDITOR_APP_DATA_DIR", QDir(agent_workflow_proof_dir)
        .filePath(QStringLiteral("runtime")).toUtf8());
    qputenv("PHOTO_EDITOR_CACHE_DIR", QDir(agent_workflow_proof_dir)
        .filePath(QStringLiteral("cache")).toUtf8());
    qputenv("PHOTO_EDITOR_OBSERVABILITY_RUN_DIR", QDir(agent_workflow_proof_dir)
        .filePath(QStringLiteral("observability")).toUtf8());
    qputenv("PHOTO_EDITOR_DATABASE_URL", database_url.toUtf8());
    qunsetenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    const auto private_preferences = QDir(agent_workflow_proof_dir)
        .filePath(QStringLiteral("preferences"));
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, private_preferences);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, private_preferences);
  }
  const bool timeline_route_performance =
      legacy_timeline_route_performance || timeline_route_performance_lifecycle;
  QTemporaryDir timeline_route_settings;
  if (timeline_route_performance) {
    if (!timeline_route_settings.isValid()) {
      if (timeline_route_performance_lifecycle) {
        photo_editor::desktop::TimelineRoutePerformanceLifecycleProbe::
            writeCommandFailureReceipt(
                lifecycle_output_directory,
                {QStringLiteral("Private route probe preferences could not be created.")});
      }
      return EXIT_FAILURE;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
        timeline_route_settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
        timeline_route_settings.path());
  }
  QString managed_runtime_proof_dir;
  const auto scene_render_proof_argument = app.arguments().indexOf(QStringLiteral("--gaussian-scene-render-proof-dir"));
  const bool scene_render_only_proof = scene_render_proof_argument >= 0;
  const bool scene_render_offline_proof = app.arguments().contains(QStringLiteral("--gaussian-scene-offline-proof"));
  const bool scene_lighting_progressive_proof = app.arguments().contains(QStringLiteral("--gaussian-lighting-progressive-proof"));
  if (scene_lighting_progressive_proof && (!scene_render_only_proof || !app.arguments().contains(QStringLiteral("--gaussian-environment-proof")))) return EXIT_FAILURE;
  if (scene_render_offline_proof && !scene_render_only_proof) return EXIT_FAILURE;
  const int scene_render_wait_ms = scene_render_offline_proof ? 120000 : 30000;
  const auto managed_proof_argument = app.arguments().indexOf(QStringLiteral("--managed-runtime-proof-dir"));
  if (scene_render_only_proof && (managed_proof_argument >= 0 ||
      !app.arguments().contains(QStringLiteral("--gaussian-workspace-proof")) ||
      !app.arguments().contains(QStringLiteral("--gaussian-project-proof")))) return EXIT_FAILURE;
  const auto proof_argument = scene_render_only_proof ? scene_render_proof_argument : managed_proof_argument;
  if (proof_argument >= 0) {
    if (proof_argument + 1 >= app.arguments().size()) return EXIT_FAILURE;
    managed_runtime_proof_dir = app.arguments()[proof_argument + 1];
    if (!QFileInfo(managed_runtime_proof_dir).isAbsolute() ||
        !QDir().mkpath(managed_runtime_proof_dir) ||
        QFileInfo::exists(QDir(managed_runtime_proof_dir).filePath(QStringLiteral("receipt.json")))) return EXIT_FAILURE;
    // Packaged-app proof is deliberately isolated from the user's catalog,
    // preferences, model manifest and cache. Normal smoke uses a closed DB port;
    // the full export handoff proof uses a fresh managed DB under this directory.
    qputenv("PHOTO_EDITOR_APP_DATA_DIR", QDir(managed_runtime_proof_dir).filePath(QStringLiteral("runtime")).toUtf8());
    qputenv("PHOTO_EDITOR_CACHE_DIR", QDir(managed_runtime_proof_dir).filePath(QStringLiteral("cache")).toUtf8());
    if ((app.arguments().contains(QStringLiteral("--export-handoff-proof")) &&
         app.arguments().contains(QStringLiteral("--placed-layer-proof"))) ||
        app.arguments().contains(QStringLiteral("--gaussian-photo-edit-proof")) ||
        app.arguments().contains(QStringLiteral("--export-admission-proof")) ||
        app.arguments().contains(QStringLiteral("--job-recovery-isolation-proof"))) {
      qunsetenv("PHOTO_EDITOR_DATABASE_URL");
      // The fixture asset is in-memory; job persistence remains enabled.
      if (!app.arguments().contains(QStringLiteral("--gaussian-photo-edit-proof")))
        qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
      else
        qunsetenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE");
    } else
      qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/framehouse_runtime_proof");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
        QDir(managed_runtime_proof_dir).filePath(QStringLiteral("preferences")));
  }
  const auto runtime_directories_ready =
      photo_editor::desktop::AppBootstrap::ensureRuntimeDirectories();
  if (timeline_route_performance_lifecycle && !runtime_directories_ready) {
    photo_editor::desktop::TimelineRoutePerformanceLifecycleProbe::
        writeCommandFailureReceipt(
            lifecycle_output_directory,
            {QStringLiteral("Caller-supplied runtime and cache directories are unavailable.")});
    return EXIT_FAILURE;
  }
  photo_editor::desktop::observability::initialize();
  photo_editor::desktop::observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("desktop.startup.begin"),
      {
          {QStringLiteral("session_id"), photo_editor::desktop::observability::sessionId()},
          {QStringLiteral("run_directory"), photo_editor::desktop::observability::runDirectory()},
          {QStringLiteral("desktop_log_file"), photo_editor::desktop::observability::desktopLogFilePath()},
      });
  photo_editor::desktop::CustomerAccessController customer_access;
  const auto edit_access = [&customer_access] { return customer_access.editingAllowed(); };
  photo_editor::desktop::CatalogAssetModel catalog_assets;
  auto catalog_identity = QUrl(qEnvironmentVariable("PHOTO_EDITOR_DATABASE_URL"));
  catalog_identity.setUserInfo(QString());
  catalog_identity.setQuery(QString());
  catalog_identity.setFragment(QString());
  photo_editor::desktop::LibraryWorkflowController library_workflow(
      QDir(photo_editor::desktop::AppBootstrap::appDataLocation()).filePath(QStringLiteral("library-workflow.json")),
      photo_editor::desktop::AppBootstrap::appDataLocation() + QStringLiteral("|") + catalog_identity.toString());
  catalog_assets.setEditAccessPolicy(edit_access);
  catalog_assets.beginCatalogStartup();
  photo_editor::desktop::ManagedAgentRuntime managed_agent_runtime;
  photo_editor::desktop::GaussianSceneController gaussian_scene;
  gaussian_scene.setEditAccessPolicy(edit_access);
  gaussian_scene.setCapturePreparer(photo_editor::desktop::prepare_gaussian_capture);
  gaussian_scene.setVideoPreparer(photo_editor::desktop::prepare_gaussian_video);
  const auto scene_trainer_argument = app.arguments().indexOf(QStringLiteral("--scene-trainer"));
  if (scene_trainer_argument >= 0 && scene_trainer_argument + 1 < app.arguments().size()) {
    const auto configured = gaussian_scene.setTrainerExecutable(app.arguments()[scene_trainer_argument + 1]);
    if (!configured.value("ok").toBool()) qWarning().noquote() << configured.value("error").toString();
  }
  gaussian_scene.setSpacetimeValidator(photo_editor::desktop::validate_gaussian_spacetime_artifact);
  gaussian_scene.setTimedCaptureHandlers(photo_editor::desktop::prepare_gaussian_timed_capture,
                                         photo_editor::desktop::validate_prepared_gaussian_timed_capture);
  photo_editor::desktop::VisualSearchController visual_search;
  visual_search.setIndexPath(QDir(photo_editor::desktop::AppBootstrap::appDataLocation()).filePath(QStringLiteral("visual-search/descriptions.sqlite3")));
  visual_search.setUseIndex(true);
  // Explicit local opt-in only. Never infer approval to download or adopt a
  // runtime, and never hard-code the isolated proof installation into the app.
  photo_editor::desktop::CatalogSimilaritySearch similarity_search(&catalog_assets,
      qEnvironmentVariable("PHOTO_EDITOR_DATABASE_URL",
          photo_editor::desktop::AppBootstrap::appDataLocation() + "/managed-catalog"));
  const auto similarity_index = QDir(photo_editor::desktop::AppBootstrap::appDataLocation()).filePath("visual-search/embeddings.sqlite3");
  const auto similarity_model = photo_editor::desktop::search_model_location(
      photo_editor::desktop::AppBootstrap::appDataLocation(),
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation),
      qEnvironmentVariable("PHOTO_EDITOR_SEARCH_MODEL_ROOT"),
      !qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_DIR").trimmed().isEmpty());
  const auto similarity_device = qEnvironmentVariable("PHOTO_EDITOR_SEARCH_DEVICE", "cpu");
  photo_editor::desktop::SearchModelSetupController search_model_setup(
      similarity_search.controller(), similarity_model, similarity_device, similarity_index);
  QFutureWatcher<photo_editor::desktop::SearchRuntimeVerification> similarity_runtime_verification;
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SEARCH_EXECUTABLE")) {
    search_model_setup.setRuntime(qEnvironmentVariable("PHOTO_EDITOR_SEARCH_EXECUTABLE"));
  } else if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SEARCH_PYTHON") || qEnvironmentVariableIsSet("PHOTO_EDITOR_SEARCH_WORKER")) {
    similarity_search.controller()->configure(qEnvironmentVariable("PHOTO_EDITOR_SEARCH_PYTHON"),
        qEnvironmentVariable("PHOTO_EDITOR_SEARCH_WORKER"), similarity_model, similarity_device, similarity_index);
  } else {
#ifdef PHOTO_EDITOR_SEARCH_RUNTIME_RECEIPT_SHA256
    const auto runtime_root = QDir(QCoreApplication::applicationDirPath()).filePath("../Resources/search-runtime");
    similarity_search.controller()->setSetupStatus("Checking the bundled local search runtime…");
    search_model_setup.setRuntimeStatus("Checking the bundled local search runtime…");
    QObject::connect(&similarity_runtime_verification, &QFutureWatcher<photo_editor::desktop::SearchRuntimeVerification>::finished,
        &similarity_search, [&] {
      const auto checked = similarity_runtime_verification.result();
      if (checked.ok) search_model_setup.setRuntime(checked.executable);
      else {
        similarity_search.controller()->setSetupStatus(checked.error);
        search_model_setup.setRuntimeStatus(checked.error);
      }
    });
    similarity_runtime_verification.setFuture(QtConcurrent::run([runtime_root] {
      return photo_editor::desktop::verify_search_runtime_bundle(runtime_root, QStringLiteral(PHOTO_EDITOR_SEARCH_RUNTIME_RECEIPT_SHA256));
    }));
#else
    search_model_setup.setRuntimeStatus("This build does not include the local search runtime.");
#endif
  }
  photo_editor::desktop::CrashReportingController crash_reporting;
  crash_reporting.setCustomerId(customer_access.customerId());
  QObject::connect(&customer_access, &photo_editor::desktop::CustomerAccessController::changed,
                   &crash_reporting, [&] { crash_reporting.setCustomerId(customer_access.customerId()); });
  photo_editor::desktop::AgentEditController agent_edit;
  photo_editor::desktop::AgentSessionController agent_session;
  agent_edit.setAccessPolicy([&customer_access] { return customer_access.allows(QStringLiteral("agent")); });
  QObject::connect(&agent_edit, &photo_editor::desktop::AgentEditController::runningChanged,
                   &crash_reporting, [&] { crash_reporting.recordEvent(agent_edit.running()
                       ? QStringLiteral("agent.started") : QStringLiteral("agent.finished")); });
  QObject::connect(&managed_agent_runtime, &photo_editor::desktop::ManagedAgentRuntime::changed,
                   &agent_edit, &photo_editor::desktop::AgentEditController::reloadModels);
  QObject::connect(&agent_edit, &photo_editor::desktop::AgentEditController::managedModelInstallStateChanged,
                   &managed_agent_runtime, &photo_editor::desktop::ManagedAgentRuntime::setModelInstalling);
  QObject::connect(&agent_edit, &photo_editor::desktop::AgentEditController::managedRuntimeStartRequested,
                   &managed_agent_runtime, [&managed_agent_runtime] {
    managed_agent_runtime.start(photo_editor::desktop::agent_runtime_binary(),
        QDir(photo_editor::desktop::AppBootstrap::appDataLocation()).filePath(QStringLiteral("agent-runtime/models")));
  }, Qt::QueuedConnection);
  bool managed_runtime_requested = false;
  for (const auto& value : agent_edit.availableModels()) {
    if (value.toMap().value(QStringLiteral("runtime")).toString() == QStringLiteral("managed"))
      managed_runtime_requested = true;
  }
  if (managed_runtime_requested && !scene_render_only_proof) {
    QTimer::singleShot(0, &managed_agent_runtime, [&managed_agent_runtime] {
      managed_agent_runtime.start(photo_editor::desktop::agent_runtime_binary(),
          QDir(photo_editor::desktop::AppBootstrap::appDataLocation()).filePath(QStringLiteral("agent-runtime/models")));
    });
  }
  photo_editor::desktop::AppShellController app_shell;
  if (timeline_route_performance_lifecycle ||
      (!managed_runtime_proof_dir.isEmpty() && (app.arguments().contains(QStringLiteral("--managed-runtime-setup-proof")) ||
      app.arguments().contains(QStringLiteral("--recipe-save-error-proof")) ||
      app.arguments().contains(QStringLiteral("--recipe-close-proof")) ||
      app.arguments().contains(QStringLiteral("--gaussian-workspace-proof"))))) {
    // This QA profile has isolated preferences. Exercise setup after onboarding,
    // as a user would, without a delayed welcome popup covering its controls.
    app_shell.completeFirstRunExperience();
  }
  photo_editor::desktop::CarouselDocumentController carousel_document;
  carousel_document.setEditAccessPolicy(edit_access);
  const bool recipe_save_error_proof = app.arguments().contains(QStringLiteral("--recipe-save-error-proof"));
  if (app.arguments().contains(QStringLiteral("--recipe-close-error-proof")) && !recipe_save_error_proof) return EXIT_FAILURE;
  photo_editor::desktop::EditorRecipeSaveJournal proof_save_owner;
  if (recipe_save_error_proof) {
    // A second writer is an actual recoverability failure. Hold only this QA
    // profile's isolated lock, never a user catalog or an arbitrary lock path.
    if (managed_runtime_proof_dir.isEmpty() || !app.arguments().contains(QStringLiteral("--placed-layer-proof"))) return EXIT_FAILURE;
    photo_editor::desktop::EditorRecipeSaveCheckpoint recovered;
    QString error;
    if (!proof_save_owner.open(photo_editor::desktop::AppBootstrap::appDataLocation(),
        photo_editor::desktop::EditorRecipeSaveJournal::currentAuthority(), &recovered, &error)) return EXIT_FAILURE;
  }
  photo_editor::desktop::EditorPreviewController editor_preview;
  editor_preview.setEditAccessPolicy(edit_access);
  photo_editor::desktop::EditorCloseCoordinator close_coordinator(
      editor_preview.recipeSaveProcessForClose(),
      [&editor_preview](QString* error) { return editor_preview.prepareRecipeClose(error); },
      [&editor_preview](bool paused) { editor_preview.pauseRecipePersistence(paused); });
  close_coordinator.installQuitGuard(&app);
  photo_editor::desktop::JobRunner job_runner;
  photo_editor::desktop::JobQueueModel job_queue;
  photo_editor::desktop::ExportJobController export_jobs;
  photo_editor::desktop::AgentTaskCoordinator agent_tasks;
  QStringList agent_task_target_ids;
  QString mcp_timeline_task_id;
  QStringList mcp_timeline_target_ids;
  agent_session.setContextSnapshotHandler([&catalog_assets, &editor_preview, &agent_edit](const QVariantMap& supplied) {
    auto context = supplied;
    context.remove(QStringLiteral("contextError"));
    context.insert(QStringLiteral("selectedModelId"), agent_edit.selectedModelId());
    bool model_ready = false;
    for (const auto& entry : agent_edit.availableModels()) {
      const auto model = entry.toMap();
      if (model.value(QStringLiteral("id")).toString() != agent_edit.selectedModelId()) continue;
      model_ready = model.value(QStringLiteral("available")).toBool() &&
          !model.value(QStringLiteral("backend")).toString().isEmpty() &&
          (model.value(QStringLiteral("configured"), true).toBool() ||
           model.value(QStringLiteral("backend")).toString() == QStringLiteral("local"));
    }
    context.insert(QStringLiteral("selectedModelReady"), model_ready);
    QVariantMap revisions;
    QVariantList targets;
    const auto ids = context.value(QStringLiteral("targetIds")).toStringList();
    for (const auto& id : ids) {
      const auto asset = catalog_assets.assetById(id);
      if (asset.isEmpty()) {
        context.insert(QStringLiteral("contextError"), QStringLiteral("A selected asset is no longer available. Update the selection."));
        continue;
      }
      if (editor_preview.assetId() != id &&
          asset.value(QStringLiteral("mediaType")).toString() == QStringLiteral("photo") &&
          !catalog_assets.assetRecipeReady(id)) {
        catalog_assets.requestAssetRecipes({id});
        context.insert(QStringLiteral("contextError"), QStringLiteral("The selected photo's saved edit is still loading. Retry after it finishes."));
        continue;
      }
      const auto recipe = editor_preview.assetId() == id
          ? editor_preview.currentAssetBoundAdjustmentPatchJson()
          : asset.value(QStringLiteral("adjustmentPatchJson")).toString();
      const QFileInfo source(asset.value(QStringLiteral("filePath")).toString());
      if (!source.isFile())
        context.insert(QStringLiteral("contextError"), QStringLiteral("A selected source file is unavailable. Relink it before starting."));
      const auto identity = QJsonDocument::fromVariant(QVariantMap{
          {QStringLiteral("recipe"), recipe}, {QStringLiteral("path"), source.absoluteFilePath()},
          {QStringLiteral("size"), source.size()}, {QStringLiteral("modified"), source.lastModified().toMSecsSinceEpoch()}}).toJson(QJsonDocument::Compact);
      revisions.insert(id, QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex()));
      targets.append(QVariantMap{{QStringLiteral("id"), id},
          {QStringLiteral("name"), source.completeBaseName()},
          {QStringLiteral("mediaType"), asset.value(QStringLiteral("mediaType"))},
          {QStringLiteral("filePath"), source.absoluteFilePath()}});
    }
    context.insert(QStringLiteral("assetRevisions"), revisions);
    context.insert(QStringLiteral("targets"), targets);
    QVariantMap inspiration_revisions;
    QVariantList reference_assets;
    for (const auto& id : context.value(QStringLiteral("inspirationReferences")).toStringList()) {
      const auto reference = catalog_assets.assetById(id);
      const QFileInfo source(reference.value(QStringLiteral("filePath")).toString());
      if (reference.isEmpty() || !source.isFile()) {
        context.insert(QStringLiteral("contextError"), QStringLiteral("A selected inspiration photo is unavailable. Choose another reference."));
        continue;
      }
      reference_assets.append(reference);
      const auto identity = QJsonDocument::fromVariant(QVariantMap{{QStringLiteral("asset"), reference},
          {QStringLiteral("size"), source.size()}, {QStringLiteral("modified"), source.lastModified().toMSecsSinceEpoch()}}).toJson(QJsonDocument::Compact);
      inspiration_revisions.insert(id, QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex()));
    }
    QVariantMap style_revisions;
    QVariantList inspiration_looks;
    for (const auto& id : context.value(QStringLiteral("selectedStyles")).toStringList()) {
      QVariantMap selected;
      for (const auto& value : agent_edit.availableStyles()) {
        const auto style = value.toMap();
        if (style.value(QStringLiteral("id")).toString() == id) { selected = style; break; }
      }
      if (selected.isEmpty()) {
        context.insert(QStringLiteral("contextError"), QStringLiteral("A selected saved look is unavailable. Update the inspiration."));
        continue;
      }
      inspiration_looks.append(selected);
      style_revisions.insert(id, QString::fromLatin1(QCryptographicHash::hash(
          QJsonDocument::fromVariant(selected).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex()));
    }
    context.insert(QStringLiteral("inspirationRevisions"), inspiration_revisions);
    context.insert(QStringLiteral("styleRevisions"), style_revisions);
    context.insert(QStringLiteral("referenceAssets"), reference_assets);
    context.insert(QStringLiteral("inspirationLooks"), inspiration_looks);
    context.insert(QStringLiteral("contextReady"), !context.contains(QStringLiteral("contextError")));
    return context;
  });
  photo_editor::desktop::AgentPreparedCanvasTask prepared_canvas(
      catalog_assets, editor_preview, agent_edit, agent_session, agent_tasks);
  photo_editor::desktop::AgentExecutionAdmission execution_admission(
      [&editor_preview, &catalog_assets](const QVariantMap& approved) -> QVariantMap {
    if (!editor_preview.recipeSaveError().isEmpty())
      return {{"error", editor_preview.recipeSaveError()}};
    if (editor_preview.recipeSavePending())
      return {{"pending", true}, {"activity", "Waiting for the current photo saves to finish."}};
    if (editor_preview.photoPaintBusy())
      return {{"pending", true}, {"activity", "Waiting for the current brush operation to finish."}};
    const auto ids = approved.value(QStringLiteral("targetIds")).toStringList();
    for (const auto& id : ids) {
      const auto target = catalog_assets.assetById(id);
      if (target.isEmpty())
        return {{"error", "The approved photo is unavailable. Update the request before starting."}};
      if (target.value(QStringLiteral("mediaType")).toString() == QStringLiteral("photo") &&
          !catalog_assets.assetRecipeReady(id)) {
        catalog_assets.requestAssetRecipes({id});
        return {{"pending", true}, {"activity", "Loading the approved photo's saved edit."}};
      }
      if (!catalog_assets.agentTaskRecipePersistenceSettled(id))
        return {{"pending", true}, {"activity", "Waiting for the selected photos' saves to finish."}};
    }
    if (!ids.isEmpty() && editor_preview.assetId() != ids.front()) {
      const auto asset = catalog_assets.assetById(ids.front());
      const QFileInfo source(asset.value(QStringLiteral("filePath")).toString());
      if (asset.isEmpty() || !source.isFile())
        return {{"error", "The approved photo is unavailable. Update the request before starting."}};
      editor_preview.loadAsset(ids.front(), source.absoluteFilePath(),
          asset.value(QStringLiteral("thumbnailSource")).toString(),
          asset.value(QStringLiteral("cameraLabel")).toString(),
          asset.value(QStringLiteral("captureDate")).toString(),
          asset.value(QStringLiteral("lensModel")).toString(),
          asset.value(QStringLiteral("adjustmentPatchJson")).toString());
      return {{"pending", true}, {"activity", "Opening the approved photo and settling its saved state."}};
    }
    if (!ids.isEmpty() && !editor_preview.hasAsset())
      return {{"pending", true}, {"activity", "Opening the approved photo."}};
    if (!ids.isEmpty() && editor_preview.processing())
      return {{"pending", true}, {"activity", "Preparing the approved photo preview."}};
    if (!ids.isEmpty() && !editor_preview.previewFailureStage().isEmpty())
      return {{"error", editor_preview.previewFailureDetail().isEmpty()
          ? QStringLiteral("The approved photo preview could not be prepared.")
          : editor_preview.previewFailureDetail()}};
    return {{"ready", true}};
  }, [&agent_session](const QVariantMap& approved) {
    return agent_session.validateApprovedExecutionSnapshot(approved);
  }, [&agent_edit, &agent_session, &editor_preview, &prepared_canvas](const QVariantMap& approved) {
    auto snapshot = approved;
    auto execution_plan = approved.value(QStringLiteral("plan")).toMap();
    for (const auto& key : {QStringLiteral("briefVersion"), QStringLiteral("targetSnapshot"),
             QStringLiteral("contextSnapshot"), QStringLiteral("targetIds")}) execution_plan.remove(key);
    const QVariantMap execution_brief{
        {QStringLiteral("prompt"), approved.value(QStringLiteral("prompt"))},
        {QStringLiteral("answers"), approved.value(QStringLiteral("answers"))},
        {QStringLiteral("plan"), execution_plan},
        {QStringLiteral("exactCopy"), approved.value(QStringLiteral("exactCopy"))},
        {QStringLiteral("deliverables"), approved.value(QStringLiteral("deliverables"))},
        {QStringLiteral("isRevision"), approved.value(QStringLiteral("isRevision"))},
        {QStringLiteral("revisionContext"), approved.value(QStringLiteral("revisionContext"))},
        {QStringLiteral("continuationTaskId"), approved.value(QStringLiteral("continuationTaskId"))}};
    const auto instructions = QStringLiteral("Complete the approved outcome, retaining exact copy, targets and deliverables.\nApproved brief:\n") +
        QString::fromUtf8(QJsonDocument::fromVariant(execution_brief).toJson(QJsonDocument::Compact));
    snapshot.insert(QStringLiteral("approvedInstructions"), instructions);
    snapshot.insert(QStringLiteral("taskScopedCapabilities"),
        photo_editor::desktop::agent_task_capabilities_for_plan(approved.value(QStringLiteral("plan")).toMap()));
    const auto fail = [&agent_session](const QVariantMap& result) {
      auto report = result;
      report.insert(QStringLiteral("ok"), false);
      report.insert(QStringLiteral("error"), result.value(QStringLiteral("error"),
          result.value(QStringLiteral("errors")).toStringList().join(QStringLiteral(" "))));
      agent_session.acknowledgeExecution(report);
    };
    const auto ids = approved.value(QStringLiteral("targetIds")).toStringList();
    if (ids.isEmpty()) {
      const auto started = prepared_canvas.start(snapshot);
      if (!started.value(QStringLiteral("ok")).toBool()) fail(started);
      return;
    }
    if (agent_edit.draftActive()) {
      const auto task_id = approved.value(QStringLiteral("continuationTaskId")).toString();
      const auto retained_task = agent_edit.taskTransaction();
      if (task_id.isEmpty() ||
          task_id != retained_task.value(QStringLiteral("taskId")).toString() ||
          retained_task.value(QStringLiteral("status")).toString() != QStringLiteral("review")) {
        fail({{QStringLiteral("error"), QStringLiteral("Review or revert the existing draft before starting another task.")}});
        return;
      }
      const auto pinned_revision = agent_edit.approveTaskRevision(snapshot, task_id);
      if (!pinned_revision.value(QStringLiteral("ok")).toBool()) { fail(pinned_revision); return; }
      const auto started = agent_edit.reviseDraft(instructions);
      if (!started.value(QStringLiteral("ok")).toBool()) fail(started);
      return;
    }
    const auto pinned = agent_edit.setApprovedTaskSnapshot(snapshot);
    if (!pinned.value(QStringLiteral("ok")).toBool()) { fail(pinned); return; }
    if (editor_preview.assetId() != ids.front()) {
      fail({{QStringLiteral("error"), QStringLiteral("The approved photo changed before execution. Review the request again.")}});
      return;
    }
    const auto started = agent_edit.startDraftForConfirmedAssets(ids, instructions);
    if (!started.value(QStringLiteral("ok")).toBool()) fail(started);
  }, [&agent_session](const QVariantMap& failure) {
    agent_session.acknowledgeExecution(failure);
  }, [&agent_session](const QString& activity) {
    agent_session.reportExecutionActivity(activity);
  }, &agent_session);
  QObject::connect(&agent_session, &photo_editor::desktop::AgentSessionController::executionRequested,
      &execution_admission, [&agent_session, &execution_admission](const QVariantMap& approved) {
    const auto validation = agent_session.validateApprovedExecutionSnapshot(approved);
    if (!validation.value(QStringLiteral("ok")).toBool()) {
      agent_session.acknowledgeExecution(validation);
      return;
    }
    const auto accepted = execution_admission.submit(approved);
    if (!accepted.value(QStringLiteral("ok")).toBool()) agent_session.acknowledgeExecution(accepted);
  });
  QObject::connect(&agent_edit, &photo_editor::desktop::AgentEditController::draftRunFinished,
      &agent_session, [&agent_session](const QString&, const QVariantMap& report) { agent_session.acknowledgeExecution(report); });
  QObject::connect(&agent_edit, &photo_editor::desktop::AgentEditController::runEventsChanged,
      &agent_session, [&agent_edit, &agent_session] {
    if (agent_session.stage() != QStringLiteral("executing")) return;
    const auto events = agent_edit.runEvents();
    if (events.isEmpty()) return;
    const auto event = events.constLast().toMap();
    const auto stage = event.value(QStringLiteral("stage")).toString();
    const auto status = event.value(QStringLiteral("status")).toString();
    if (status == QStringLiteral("running") || status == QStringLiteral("complete")) {
      QString activity;
      if (stage.startsWith(QStringLiteral("worker-composition-detail-proof")))
        activity = QStringLiteral("Checking the title and subject edges.");
      else if (stage.startsWith(QStringLiteral("worker-planning-proof")))
        activity = QStringLiteral("Inspecting the source photo.");
      else if (stage.startsWith(QStringLiteral("worker-proof-render")))
        activity = QStringLiteral("Rendering the edit for review.");
      else if (stage.startsWith(QStringLiteral("worker-retouch-materialization")))
        activity = QStringLiteral("Applying the planned local edits.");
      if (!activity.isEmpty()) {
        agent_session.reportExecutionActivity(status == QStringLiteral("complete")
            ? QStringLiteral("Finished: %1").arg(activity) : activity);
        return;
      }
    }
    const auto detail = event.value(QStringLiteral("explanation")).toString().trimmed();
    agent_session.reportExecutionActivity(detail.isEmpty()
        ? event.value(QStringLiteral("label")).toString() : detail);
  });
  QObject::connect(&agent_edit, &photo_editor::desktop::AgentEditController::taskTransactionChanged,
      &agent_session, [&agent_edit, &agent_session] {
    const auto transaction = agent_edit.taskTransaction();
    if (transaction.value(QStringLiteral("status")) == QStringLiteral("reverted"))
      agent_session.acknowledgeExecution({{QStringLiteral("ok"), true},
          {QStringLiteral("status"), QStringLiteral("reverted")},
          {QStringLiteral("taskTransaction"), transaction},
          {QStringLiteral("headline"), QStringLiteral("The task was reverted. Your prompt and answers are saved.")}});
  });
  agent_edit.setCatalogModel(&catalog_assets);
  agent_edit.setExportJobController(&export_jobs);
  agent_session.setRevertAvailabilityHandler([&agent_edit, &agent_session, &prepared_canvas]() {
    const auto transaction = agent_edit.taskTransaction();
    const auto canvas_owner = prepared_canvas.preparing() || prepared_canvas.ownsTask();
    const auto photo_owner = agent_edit.draftActive() || agent_edit.canRevertTask();
    const auto recovery_settling = agent_session.stage() != QStringLiteral("executing") &&
        (agent_edit.running() || agent_edit.draftRequestPending() ||
         !agent_edit.activeDraftRequestId().isEmpty() ||
         !agent_edit.pendingTaskFinalization().isEmpty());
    return QVariantMap{
        {QStringLiteral("available"), canvas_owner || photo_owner},
        {QStringLiteral("pending"),
            recovery_settling || transaction.value(QStringLiteral("pending")).toBool() ||
            transaction.value(QStringLiteral("finalizationPending")).toBool()},
        {QStringLiteral("owner"), canvas_owner ? QStringLiteral("prepared-canvas")
            : photo_owner ? QStringLiteral("photo-task") : QString{}},
    };
  });
  QObject::connect(&agent_edit, &photo_editor::desktop::AgentEditController::draftChanged,
      &agent_session, &photo_editor::desktop::AgentSessionController::notifyRevertAvailabilityChanged);
  QObject::connect(&agent_edit, &photo_editor::desktop::AgentEditController::taskTransactionChanged,
      &agent_session, &photo_editor::desktop::AgentSessionController::notifyRevertAvailabilityChanged);
  QObject::connect(&agent_edit, &photo_editor::desktop::AgentEditController::runningChanged,
      &agent_session, &photo_editor::desktop::AgentSessionController::notifyRevertAvailabilityChanged);
  QObject::connect(&agent_edit, &photo_editor::desktop::AgentEditController::draftRequestChanged,
      &agent_session, &photo_editor::desktop::AgentSessionController::notifyRevertAvailabilityChanged);
  agent_session.setExecutionControlHandler([&agent_edit, &agent_session, &prepared_canvas, &execution_admission](const QString& action) {
    if (execution_admission.pending()) {
      auto canceled = execution_admission.cancel();
      if (!canceled.value(QStringLiteral("ok")).toBool()) return canceled;
      if (action == QStringLiteral("stop") ||
          (!agent_edit.draftActive() && !prepared_canvas.preparing() && !prepared_canvas.ownsTask())) {
        canceled.insert(QStringLiteral("headline"),
            QStringLiteral("The task stopped before editing began. Your prompt and answers are saved."));
        agent_session.acknowledgeExecution(canceled);
        return canceled;
      }
    }
    if (prepared_canvas.preparing() || prepared_canvas.ownsTask()) return prepared_canvas.control(action);
    return action == QStringLiteral("stop") ? agent_edit.cancelRun(QStringLiteral("user_requested"))
        : agent_edit.revertTask();
  });
  photo_editor::desktop::SearchEditWorkflow search_edit(&catalog_assets);
  search_edit.setDraftBackend([&agent_edit] {
    return agent_edit.running() || agent_edit.draftActive() || agent_edit.awaitingUserInput();
  }, [&agent_edit, &search_edit](const QStringList& ids, const QString& instructions) {
    const auto model_id = search_edit.search()->modelConfiguration().value(QStringLiteral("id")).toString();
    agent_edit.setSelectedModelId(model_id);
    if (model_id.isEmpty() || agent_edit.selectedModelId() != model_id)
      return QVariantMap{{"ok", false}, {"errors", QStringList{QStringLiteral("The searched local model is no longer available. Search again.")}}};
    agent_edit.setUserInstructions(instructions);
    return agent_edit.startDraftForConfirmedAssets(ids, instructions);
  });
  QObject::connect(&agent_edit, &photo_editor::desktop::AgentEditController::draftRunFinished,
                   &search_edit, &photo_editor::desktop::SearchEditWorkflow::draftFinished);
  editor_preview.setAgentTaskJournal(agent_tasks.journal());
  photo_editor::desktop::AgentTaskRecipePersistence agent_recipe_saves(editor_preview, catalog_assets, agent_tasks);
  editor_preview.setAgentRecipePersistenceHandler([&agent_recipe_saves](const QString& stage, const QVariantMap& context) {
    return agent_recipe_saves.handle(stage, context);
  });
  catalog_assets.setAgentRecipePersistenceHandler([&agent_recipe_saves](const QString& stage, const QVariantMap& context) {
    return agent_recipe_saves.handle(stage, context);
  });
  QVariantMap pending_task_settlement;
  const auto task_saves_pending = [&]() {
    if (editor_preview.recipeSavePending() || agent_recipe_saves.pending()) return true;
    for (const auto& id : agent_task_target_ids)
      if (!catalog_assets.agentTaskRecipePersistenceSettled(id)) return true;
    return false;
  };
  agent_recipe_saves.setSettlementHandler([&]() {
    if (!agent_recipe_saves.pending())
      catalog_assets.resumeAgentAdjustmentPatchPersistence(agent_tasks.journal()->taskId());
    if (pending_task_settlement.isEmpty()) return;
    auto result = QVariantMap{};
    if (!agent_recipe_saves.error().isEmpty()) {
      result = {{"ok", false}, {"error", agent_recipe_saves.error()}};
    } else if (task_saves_pending()) {
      return;
    } else if (!editor_preview.recipeSaveError().isEmpty()) {
      result = {{"ok", false}, {"error", editor_preview.recipeSaveError()}};
    } else {
      const auto action = pending_task_settlement.value("action").toString();
      result = action == QStringLiteral("revert") ? agent_tasks.revert() : agent_tasks.finish(action);
    }
    const auto task = pending_task_settlement.value("taskId").toString();
    result.insert("taskId", task);
    result.insert("receiptId", pending_task_settlement.value("receiptId"));
    if (result.value("ok").toBool()) pending_task_settlement.clear();
    else {
      result.insert("pending", true);
      result.insert("retryable", true);
    }
    static_cast<void>(agent_edit.settleTaskFinalization(task, result));
  });
  photo_editor::desktop::AgentAppCommandRegistry app_commands;
  photo_editor::desktop::register_agent_photo_commands(app_commands, &editor_preview,
      [&agent_edit]() { return agent_edit.activeTaskId(); },
      [&agent_edit](const QString& name, bool property) { return agent_edit.authorizeNativeEditingCommand(name, property); });
  photo_editor::desktop::PhotoAutomationBridge photo_automation(&agent_edit, &catalog_assets);
  photo_automation.setAppCommandRegistry(&app_commands);
  photo_automation.setEditAccessPolicy(edit_access);
  photo_automation.setTimelineJobQueueModel(&job_queue);
  agent_edit.setTaskTransactionHandler([&editor_preview, &catalog_assets, &agent_tasks, &agent_task_target_ids,
      &agent_recipe_saves, &pending_task_settlement, &task_saves_pending](const QString& action, const QString& task_id,
      const QStringList& asset_ids, const QVariantMap& context) -> QVariantMap {
    if (action == QStringLiteral("begin")) {
      if (editor_preview.recipeSavePending())
        return {{"ok", false}, {"error", "Wait for the current photo saves before starting this task."}};
      const auto begun = agent_tasks.begin(photo_editor::desktop::AppBootstrap::appDataLocation(), task_id);
      if (!begun.value("ok").toBool()) return begun;
      const auto approved = context.value(QStringLiteral("approvedTaskSnapshot")).toMap();
      if (!approved.isEmpty()) {
        const auto persisted = agent_tasks.journal()->recordApprovedSnapshot(approved);
        if (!persisted.value(QStringLiteral("ok")).toBool()) {
          static_cast<void>(agent_tasks.abandonAdmission());
          return persisted;
        }
      }
      agent_task_target_ids = asset_ids;
      for (const auto& id : asset_ids) {
        if (!catalog_assets.agentTaskRecipePersistenceSettled(id)) {
          static_cast<void>(agent_tasks.abandonAdmission());
          return {{"ok", false}, {"error", "Wait for the selected photos' catalog saves before starting this task."}};
        }
        const auto stored = agent_tasks.enroll(QStringLiteral("catalog-recipe:%1").arg(id),
            catalog_assets.agentTaskRecipeResourceAdapter(id, task_id));
        if (!stored.value("ok").toBool()) {
          static_cast<void>(agent_tasks.abandonAdmission());
          return stored;
        }
      }
      if (editor_preview.assetId().isEmpty() || !asset_ids.contains(editor_preview.assetId()))
        return begun;
      const auto photo = editor_preview.beginAgentTask(task_id);
      if (!photo.value("ok").toBool()) {
        static_cast<void>(agent_tasks.abandonAdmission());
        return photo;
      }
      const auto enrolled = agent_tasks.enroll(editor_preview.assetId(), editor_preview.agentTaskResourceAdapter(task_id));
      if (!enrolled.value("ok").toBool()) {
        static_cast<void>(agent_tasks.abandonAdmission());
        return enrolled;
      }
      return photo;
    }
    if (agent_tasks.journal()->taskId() != task_id)
      return {{"ok", false}, {"error", "The requested task is not active."}};
    if (action == QStringLiteral("adopt_prepared")) {
      if (context.value(QStringLiteral("preparedTaskId")).toString() != task_id ||
          !context.value(QStringLiteral("requireExistingJournal")).toBool() ||
          !context.value(QStringLiteral("requireResourceCheckpoints")).toBool())
        return {{"ok", false}, {"error", "Prepared document adoption requires the original task and resource checkpoints."}};
      if (asset_ids.size() != 1 || editor_preview.assetId() != asset_ids.front())
        return {{"ok", false}, {"error", "Open the prepared canvas before starting its approved edit."}};
      if (agent_tasks.journal()->checkpointHash(asset_ids.front()).isEmpty()) {
        const auto photo = editor_preview.beginAgentTask(task_id);
        if (!photo.value("ok").toBool()) return photo;
        const auto enrolled = agent_tasks.enroll(asset_ids.front(), editor_preview.agentTaskResourceAdapter(task_id));
        if (!enrolled.value("ok").toBool()) return enrolled;
      }
      const auto adopted = agent_tasks.adoptPreparedTask(task_id, asset_ids);
      if (adopted.value("ok").toBool()) agent_task_target_ids = asset_ids;
      return adopted;
    }
    if (action == QStringLiteral("continue")) {
      for (const auto& id : asset_ids)
        if (!agent_task_target_ids.contains(id) || agent_tasks.journal()->checkpointHash(id).isEmpty())
          return {{"ok", false}, {"error", "An original photo task checkpoint is unavailable. Its draft was preserved."}};
      const auto continued = agent_tasks.continueTask();
      if (!continued.value(QStringLiteral("ok")).toBool()) return continued;
      const auto approved = context.value(QStringLiteral("approvedTaskSnapshot")).toMap();
      return approved.isEmpty() ? continued : agent_tasks.journal()->recordApprovedSnapshot(approved);
    }
    if (action == QStringLiteral("cancel")) {
      agent_tasks.cancel();
      if (!pending_task_settlement.isEmpty() && pending_task_settlement.value("taskId") == task_id)
        pending_task_settlement.insert("action", QStringLiteral("revert"));
      return agent_tasks.state();
    }
    if (action == QStringLiteral("revert") || action == QStringLiteral("check_revert")) {
      if (!agent_tasks.state().value("revertAvailable").toBool())
        return action == QStringLiteral("check_revert") ? agent_tasks.state() : agent_tasks.abandonAdmission();
      if (task_saves_pending()) {
        if (action == QStringLiteral("check_revert"))
          return {{"ok", true}, {"pending", true}, {"taskId", task_id}, {"revertAvailable", true}};
        const auto receipt_id = pending_task_settlement.isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
            : pending_task_settlement.value("receiptId").toString();
        pending_task_settlement = {{"action", action}, {"taskId", task_id}, {"receiptId", receipt_id}};
        return {{"ok", true}, {"pending", true}, {"taskId", task_id}, {"receiptId", receipt_id}};
      }
      return agent_tasks.revert(action == QStringLiteral("check_revert"));
    }
    const auto authored_recipe = context.value("expectedRecipes").toMap().value(editor_preview.assetId()).toString();
    if (!authored_recipe.isEmpty() && !agent_tasks.journal()->checkpointHash(editor_preview.assetId()).isEmpty()) {
      const auto checkpoint = editor_preview.checkpointAgentTaskRecipe(task_id, authored_recipe);
      if (!checkpoint.value("ok").toBool()) return checkpoint;
    }
    if (!agent_recipe_saves.error().isEmpty())
      return {{"ok", false}, {"error", agent_recipe_saves.error()}};
    if (task_saves_pending()) {
      const auto receipt_id = pending_task_settlement.isEmpty()
          ? QUuid::createUuid().toString(QUuid::WithoutBraces)
          : pending_task_settlement.value("receiptId").toString();
      pending_task_settlement = {{"action", action}, {"taskId", task_id}, {"receiptId", receipt_id}};
      return {{"ok", true}, {"pending", true}, {"taskId", task_id}, {"receiptId", receipt_id}};
    }
    return agent_tasks.finish(action);
  });
  photo_automation.setCompositionHandler([&editor_preview, &agent_edit](const QVariantMap& request) -> QVariantMap {
    if (request.value("operation") != QStringLiteral("inspect") && editor_preview.photoAuthoringDraftActive())
      return {{"ok", false}, {"error", QStringLiteral("Finish or cancel the current authoring draft before automation edits this document.")},
          {"errorCode", QStringLiteral("authoring_session_active")}, {"recoverable", true},
          {"blockedByResourceOwnership", true}, {"mutationNotApplied", true}};
    const auto task_id = agent_edit.activeTaskId();
    if (!task_id.isEmpty() && request.value("operation") != QStringLiteral("inspect"))
      return editor_preview.runAgentTaskComposition(task_id, request);
    return editor_preview.photoCompositionCommand(request);
  });
  agent_edit.setPlanningSnapshotHandler([&editor_preview, &catalog_assets, &agent_tasks](const QString& asset_id) -> QVariantMap {
    if (editor_preview.assetId() == asset_id) {
      if (editor_preview.photoPaintBusy())
        return {{"ok", false}, {"error", "Wait for the owned photo pixels to finish before rendering planning evidence."}};
      return {{"ok", true}, {"assetId", asset_id},
          {"adjustmentPatchJson", editor_preview.currentAssetBoundAdjustmentPatchJson()},
          {"source", "native-photo"},
          {"taskStateHash", agent_tasks.journal()->checkpointHash(asset_id)}};
    }
    const auto asset = catalog_assets.assetById(asset_id);
    if (asset.isEmpty())
      return {{"ok", false}, {"error", "The selected photo is no longer available."}};
    if (asset.value(QStringLiteral("mediaType")).toString() == QStringLiteral("photo") &&
        !catalog_assets.assetRecipeReady(asset_id)) {
      catalog_assets.requestAssetRecipes({asset_id});
      return {{"ok", false}, {"pending", true},
          {"error", "The saved edit recipe is still loading. Retry after it finishes."}};
    }
    return {{"ok", !asset.isEmpty()}, {"assetId", asset_id},
        {"adjustmentPatchJson", asset.value("adjustmentPatchJson")}, {"source", "catalog-photo"}};
  });
  photo_automation.setSearchEditWorkflow(&search_edit);
  agent_edit.setMcpEndpoint(photo_automation.agentMcpEndpoint(), QStringLiteral("in-process"));
  QTimer::singleShot(0, &agent_edit, [&agent_edit]() { agent_edit.refreshMcpTools(); });
  const auto automation_enabled = automationRequested(QCoreApplication::arguments());
  const auto automation_capabilities = requestedAutomationCapabilities();
  if (photo_automation.start(automation_enabled, automation_capabilities)) {
    photo_editor::desktop::observability::logInfo(
        photoEditorAppLog(),
        QStringLiteral("automation.bridge.started"),
        {
            {QStringLiteral("endpoint"), photo_automation.endpointName()},
            {QStringLiteral("mcp_endpoint"), photo_automation.mcpEndpoint()},
            {QStringLiteral("mcp_endpoint_kind"), photo_automation.mcpEndpointKind()},
            {QStringLiteral("session_file"), photo_automation.sessionFilePath()},
            {QStringLiteral("automation_enabled"), automation_enabled},
            {QStringLiteral("automation_capabilities"), photo_automation.mcpCapabilities()},
        });
  } else {
    photo_editor::desktop::observability::logWarning(
        photoEditorAppLog(),
        QStringLiteral("automation.bridge.failed"),
        {{QStringLiteral("error"), photo_automation.errorMessage()}});
  }
  photo_editor::desktop::MacosUpdater macos_updater;
  photo_editor::desktop::PreviewBackendController preview_backend;
  photo_editor::desktop::QmlPerformanceMonitor qml_performance;
  photo_editor::desktop::TimelineDocumentController timeline_document;
  timeline_document.setEditAccessPolicy(edit_access);
  photo_editor::desktop::TimelineColorEditController timeline_color_editor(&timeline_document, &editor_preview);
  if (timeline_route_performance &&
      !app.arguments().contains(QStringLiteral("--timeline-route-performance-empty")) &&
      !app.arguments().contains(QStringLiteral("--timeline-route-performance-creator-setup"))) {
    timeline_document.createTimelineWithSettings(
        {{QStringLiteral("timelineName"), QStringLiteral("Route Performance Fixture")},
         {QStringLiteral("width"), 1080},
         {QStringLiteral("height"), 1920},
         {QStringLiteral("frameRate"), 30},
         {QStringLiteral("audioSampleRate"), 48000},
         {QStringLiteral("colorSpace"), QStringLiteral("Rec.709")},
         {QStringLiteral("dataLevels"), QStringLiteral("Legal")},
         {QStringLiteral("trackLayout"),
          app.arguments().contains(QStringLiteral("--timeline-route-performance-creator-layout"))
              ? QStringLiteral("creator_social")
              : QStringLiteral("standard")}});
  }
  photo_automation.setTimelineDocumentController(&timeline_document);
  photo_editor::desktop::AgentTimelineCompoundCommands timeline_compound_commands(
      &timeline_document, &agent_tasks,
      [&mcp_timeline_task_id]() { return mcp_timeline_task_id; },
      [&agent_edit, &agent_tasks](const QString&) {
        return agent_edit.authorizeNativeEditingCommandForTask(
            QStringLiteral("Timeline"), false,
            agent_tasks.state().value(QStringLiteral("approvedSnapshot")).toMap());
      });
  agent_tasks.setBeforeRevertHandler([&timeline_compound_commands](const bool check_only) {
    return check_only ? timeline_compound_commands.checkBeforeRevert()
                      : timeline_compound_commands.reconcileBeforeRevert();
  });
  photo_automation.setTimelineTaskLifecycleHandler(
      [&timeline_document, &agent_tasks, &agent_edit, &edit_access, &mcp_timeline_task_id,
       &mcp_timeline_target_ids, &timeline_compound_commands](const QString& action, const QVariantMap& args) -> QVariantMap {
    const auto failure = [](const QString& message) {
      return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
    };
    const auto timeline_adapter = [&timeline_document](const QString& resource_id) {
      return photo_editor::desktop::AgentTaskJournal::ResourceAdapter{
          [&timeline_document, resource_id]() {
            return timeline_document.agentTaskDocumentId() == resource_id
                ? timeline_document.agentTaskDocumentState() : QByteArray{};
          },
          [&timeline_document, resource_id](const QByteArray& bytes) {
            return timeline_document.agentTaskDocumentId() == resource_id &&
                timeline_document.restoreAgentTaskDocument(bytes);
          }, {}};
    };
    if (action == QStringLiteral("cancel_preparation")) {
      if (!mcp_timeline_task_id.isEmpty() || agent_tasks.journal()->active() ||
          !agent_edit.activeTaskId().isEmpty() || agent_edit.draftActive() ||
          agent_edit.running()) {
        return failure(QStringLiteral(
            "Timeline preparation can only be canceled before a task is admitted."));
      }
      return timeline_document.cancelAgentTaskDocumentPreparationForAutomation();
    }
    if (action == QStringLiteral("admit")) {
      if (!args.value(QStringLiteral("confirmed")).toBool())
        return failure(QStringLiteral("Timeline task admission requires confirmed:true."));
      if (!edit_access()) return failure(QStringLiteral("Editing requires an active trial or purchase."));
      if (!mcp_timeline_task_id.isEmpty() || agent_tasks.journal()->active() ||
          !agent_edit.activeTaskId().isEmpty() || agent_edit.draftActive() || agent_edit.running())
        return failure(QStringLiteral("Finish or revert the existing agent task before admitting a Timeline task."));
      const auto target_id = args.value(QStringLiteral("targetResourceId")).toString().trimmed();
      if (target_id.isEmpty() || target_id != timeline_document.agentTaskDocumentId())
        return failure(QStringLiteral("The requested stable Timeline target is not the open document."));
      const auto objective = args.value(QStringLiteral("objective")).toString().trimmed();
      if (objective.isEmpty() || objective.size() > 5000)
        return failure(QStringLiteral("Timeline task objective must contain 1 to 5000 characters."));
      const auto actions = args.value(QStringLiteral("actions")).toList();
      const auto capabilities = photo_editor::desktop::agent_timeline_task_capabilities_for_plan({
          {QStringLiteral("actions"), actions}});
      if (actions.isEmpty() || actions.size() > 9 || capabilities.isEmpty())
        return failure(QStringLiteral("Timeline task actions must be a nonempty supported plan of up to nine approved actions."));
      const auto asset_paths = args.value(QStringLiteral("assetPaths")).toStringList();
      if (asset_paths.size() > 256)
        return failure(QStringLiteral("A Timeline task may admit at most 256 source files."));
      QSet<QString> seen_paths;
      for (const auto& path : asset_paths) {
        const QFileInfo info(path);
        if (path.trimmed().isEmpty() || seen_paths.contains(path) || !info.isFile() ||
            !info.isReadable() || info.canonicalFilePath() != path)
          return failure(QStringLiteral("Every source must be a unique canonical existing readable regular file."));
        seen_paths.insert(path);
      }
      const auto output_directory = args.value(QStringLiteral("outputDirectory")).toString().trimmed();
      const QFileInfo output_info(output_directory);
      if (output_directory.isEmpty() || output_info.canonicalFilePath() != output_directory ||
          !output_info.isDir() || !output_info.isWritable())
        return failure(QStringLiteral("The project output directory must be an existing canonical writable directory."));

      const auto preparation = timeline_document.prepareAgentTaskDocumentForAutomation();
      if (!preparation.value(QStringLiteral("ok")).toBool()) {
        return failure(preparation.value(QStringLiteral("error")).toString());
      }
      if (!preparation.value(QStringLiteral("ready")).toBool()) {
        auto pending = failure(preparation.value(QStringLiteral("message")).toString());
        pending.insert(QStringLiteral("preparationPhase"),
                       preparation.value(QStringLiteral("phase")));
        return pending;
      }

      const auto task_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
      const auto begun = agent_tasks.begin(
          photo_editor::desktop::AppBootstrap::appDataLocation(), task_id);
      if (!begun.value(QStringLiteral("ok")).toBool()) {
        timeline_document.releaseAgentTaskDocumentForAutomation();
        return begun;
      }
      const QVariantMap approved_snapshot{
          {QStringLiteral("contract"), QStringLiteral("agent-approved-session-v1")},
          {QStringLiteral("prompt"), objective},
          {QStringLiteral("targetIds"), QStringList{target_id}},
          {QStringLiteral("actions"), actions},
          {QStringLiteral("taskScopedCapabilities"), capabilities}};
      const auto approved = agent_tasks.journal()->recordApprovedSnapshot(approved_snapshot);
      if (!approved.value(QStringLiteral("ok")).toBool()) {
        static_cast<void>(agent_tasks.abandonAdmission());
        timeline_document.releaseAgentTaskDocumentForAutomation();
        return approved;
      }
      const auto scope = agent_tasks.journal()->recordNativeTimelineScope(
          {target_id}, asset_paths, output_directory);
      if (!scope.value(QStringLiteral("ok")).toBool()) {
        static_cast<void>(agent_tasks.abandonAdmission());
        timeline_document.releaseAgentTaskDocumentForAutomation();
        return scope;
      }
      const auto enrolled = agent_tasks.enroll(target_id, timeline_adapter(target_id));
      if (!enrolled.value(QStringLiteral("ok")).toBool()) {
        static_cast<void>(agent_tasks.abandonAdmission());
        timeline_document.releaseAgentTaskDocumentForAutomation();
        return enrolled;
      }
      mcp_timeline_task_id = task_id;
      mcp_timeline_target_ids = {target_id};
      const auto accepted_preparation =
          timeline_document.acceptAgentTaskDocumentPreparationForAutomation();
      if (!accepted_preparation.value(QStringLiteral("ok")).toBool()) {
        mcp_timeline_task_id.clear();
        mcp_timeline_target_ids.clear();
        static_cast<void>(agent_tasks.abandonAdmission());
        timeline_document.releaseAgentTaskDocumentForAutomation();
        return accepted_preparation;
      }
      auto receipt = agent_tasks.state();
      receipt.insert(QStringLiteral("active"), true);
      receipt.insert(QStringLiteral("targetResourceId"), target_id);
      return receipt;
    }
    if (action == QStringLiteral("status")) {
      if (mcp_timeline_task_id.isEmpty() || agent_tasks.journal()->taskId() != mcp_timeline_task_id)
        return {{QStringLiteral("ok"), true}, {QStringLiteral("active"), false}};
      auto receipt = agent_tasks.state();
      receipt.insert(QStringLiteral("active"), true);
      receipt.insert(QStringLiteral("targetResourceIds"), mcp_timeline_target_ids);
      return receipt;
    }
    if (action == QStringLiteral("recover")) {
      const auto task_id = args.value(QStringLiteral("taskId")).toString().trimmed();
      const auto target_id = args.value(QStringLiteral("targetResourceId")).toString().trimmed();
      if (task_id.isEmpty() || target_id.isEmpty()) return failure(QStringLiteral("Task and stable Timeline target IDs are required."));
      auto source_integrity_reconciled = false;
      auto compound_projection_reconciled = false;
      auto prepared_batch_recovered_unchanged = false;
      QVariant recovered_prepared_step_id;
      if (mcp_timeline_task_id.isEmpty()) {
        if (agent_tasks.journal()->active() || !agent_edit.activeTaskId().isEmpty() ||
            agent_edit.draftActive() || agent_edit.running())
          return failure(QStringLiteral("Another agent task is active and must be resolved first."));
        if (target_id != timeline_document.agentTaskDocumentId())
          return failure(QStringLiteral("Recovery requires the exact open Timeline resource stored in the journal."));
        photo_editor::desktop::AgentTaskJournal probe;
        const auto preview = probe.recover(
            photo_editor::desktop::AppBootstrap::appDataLocation(), task_id);
        if (!preview.value(QStringLiteral("ok")).toBool()) return preview;
        const auto scope = probe.nativeTimelineScope();
        const auto target_ids = scope.value(QStringLiteral("targetIds")).toStringList();
        const auto output_directory = scope.value(QStringLiteral("outputDirectory")).toString();
        const QFileInfo output_info(output_directory);
        if (target_ids != QStringList{target_id} || output_info.canonicalFilePath() != output_directory ||
            !output_info.isDir())
          return failure(QStringLiteral("Recovered task scope does not match the requested Timeline and output directory."));
        const auto preview_status = preview.value(QStringLiteral("status")).toString();
        const auto preview_steps = preview.value(QStringLiteral("steps")).toList();
        if (preview_status == QStringLiteral("running") && preview_steps.isEmpty()) {
          const auto preview_resources = preview.value(QStringLiteral("resources")).toList();
          if (preview.value(QStringLiteral("uncertainOperation")).toBool() ||
              preview_resources.size() != 1 ||
              preview_resources.front().toMap().value(QStringLiteral("id")).toString() != target_id ||
              preview_resources.front().toMap().value(QStringLiteral("pending")).toBool() ||
              preview_resources.front().toMap().value(QStringLiteral("beforeHash")).toString() !=
                  preview_resources.front().toMap().value(QStringLiteral("currentHash")).toString()) {
            return failure(QStringLiteral("A running Timeline task with no completed steps must have one exact unchanged baseline before source recovery."));
          }
          if (!timeline_document.agentTaskDocumentReady()) {
            return {{QStringLiteral("ok"), false},
                {QStringLiteral("retryable"), true},
                {QStringLiteral("status"), QStringLiteral("source_integrity_pending")},
                {QStringLiteral("taskOwnershipAttached"), false},
                {QStringLiteral("preparationStarted"), false},
                {QStringLiteral("error"), QStringLiteral("Let the open Timeline finish its current derived checks before recovering this untouched task. No preparation or task ownership was started.")}};
          }
          const auto current_state = timeline_document.agentTaskDocumentState();
          if (current_state.isEmpty())
            return failure(QStringLiteral("The recovered Timeline has no stable state for source identity verification."));
          const auto reconciled = probe.reconcileUnmutatedTimelineSourceIntegrity(target_id, current_state);
          if (!reconciled.value(QStringLiteral("ok")).toBool()) return reconciled;
          source_integrity_reconciled = reconciled.value(QStringLiteral("changed")).toBool();
        }
        if (preview.value(QStringLiteral("status")).toString() == QStringLiteral("released") &&
            !timeline_document.agentTaskDocumentReady()) {
          return {{QStringLiteral("ok"), false}, {QStringLiteral("retryable"), true},
              {QStringLiteral("status"), QStringLiteral("released_timeline_derived_work_pending")},
              {QStringLiteral("error"), QStringLiteral("Let the released Timeline finish its admitted source and metadata checks, then retry recovery. No task ownership was attached.")}};
        }
        for (const auto& value : preview.value(QStringLiteral("resources")).toList()) {
          const auto id = value.toMap().value(QStringLiteral("id")).toString();
          if (id == target_id) continue;
          bool output_resource_found = false;
          for (const auto& path : probe.nativeTimelineOutputPaths()) {
            const auto canonical_path = canonicalOutputFilePath(path);
            const auto file_resource = photo_editor::desktop::AgentTaskCoordinator::taskOwnedTimelineOutputFileResource(canonical_path);
            if (path == canonical_path && pathIsWithinDirectory(output_directory, canonical_path) &&
                file_resource.resource_id == id) {
              output_resource_found = true;
              break;
            }
          }
          if (!output_resource_found)
            return failure(QStringLiteral("The recovered task contains a resource outside its declared Timeline scope."));
        }
        const auto preparation = timeline_document.prepareAgentTaskDocumentForAutomation();
        if (!preparation.value(QStringLiteral("ok")).toBool()) {
          return failure(preparation.value(QStringLiteral("error")).toString());
        }
        if (!preparation.value(QStringLiteral("ready")).toBool()) {
          auto pending = failure(preparation.value(QStringLiteral("message")).toString());
          pending.insert(QStringLiteral("preparationPhase"),
                         preparation.value(QStringLiteral("phase")));
          pending.insert(QStringLiteral("retryable"), true);
          pending.insert(QStringLiteral("preparationRetained"), true);
          pending.insert(QStringLiteral("taskOwnershipAttached"), false);
          return pending;
        }
        if (preview_status == QStringLiteral("running")) {
          const auto current_state = timeline_document.agentTaskDocumentState();
          if (current_state.isEmpty()) {
            timeline_document.releaseAgentTaskDocumentForAutomation();
            return failure(QStringLiteral(
                "The opened Timeline does not match the recovered task's exact current checkpoint. Recovery was not attached; preserve both states."));
          }
          if (!probe.matchesCheckpoint(target_id, current_state)) {
            const auto compound_proof =
                photo_editor::desktop::AgentTimelineCompoundCommands::verifyPendingCompoundRecovery(
                    preview, target_id, current_state);
            if (!compound_proof.value(QStringLiteral("ok")).toBool()) {
              timeline_document.releaseAgentTaskDocumentForAutomation();
              return {{QStringLiteral("ok"), false},
                      {QStringLiteral("error"), QStringLiteral(
                          "The opened Timeline differs from its task checkpoint and does not satisfy the narrow pending-compound recovery proof.")},
                      {QStringLiteral("compoundRecoveryProofError"),
                       compound_proof.value(QStringLiteral("error"))}};
            }
            compound_projection_reconciled = true;
          }
        }
        const auto recovered = agent_tasks.recover(
            photo_editor::desktop::AppBootstrap::appDataLocation(), task_id);
        if (!recovered.value(QStringLiteral("ok")).toBool()) {
          timeline_document.releaseAgentTaskDocumentForAutomation();
          return recovered;
        }
        mcp_timeline_task_id = task_id;
        mcp_timeline_target_ids = target_ids;
      } else if (mcp_timeline_task_id != task_id || agent_tasks.journal()->taskId() != task_id) {
        return failure(QStringLiteral("A different native Timeline task is already active."));
      } else {
        if (!timeline_document.agentTaskDocumentReady()) {
          return {{QStringLiteral("ok"), false},
              {QStringLiteral("retryable"), true},
              {QStringLiteral("status"), QStringLiteral("timeline_derived_work_pending")},
              {QStringLiteral("taskOwnershipAttached"), true},
              {QStringLiteral("preparationRetained"), true},
              {QStringLiteral("error"), QStringLiteral("The recovered task remains attached while native Timeline work settles; retry recovery after it finishes.")}};
        }
      }
      const auto scope = agent_tasks.journal()->nativeTimelineScope();
      if (scope.value(QStringLiteral("targetIds")).toStringList() != QStringList{target_id} ||
          timeline_document.agentTaskDocumentId() != target_id || !timeline_document.agentTaskDocumentReady())
        return failure(QStringLiteral("The active Timeline does not match the recovered task target."));
      const auto enrolled = agent_tasks.enroll(target_id, timeline_adapter(target_id));
      if (!enrolled.value(QStringLiteral("ok")).toBool()) return enrolled;
      const auto output_directory = scope.value(QStringLiteral("outputDirectory")).toString();
      for (const auto& path : agent_tasks.journal()->nativeTimelineOutputPaths()) {
        const auto canonical_path = canonicalOutputFilePath(path);
        if (path != canonical_path || !pathIsWithinDirectory(output_directory, canonical_path))
          return failure(QStringLiteral("A recovered task output path escaped its admitted directory."));
        const auto file_resource = photo_editor::desktop::AgentTaskCoordinator::taskOwnedTimelineOutputFileResource(canonical_path);
        if (file_resource.resource_id.isEmpty()) return failure(QStringLiteral("A recovered task output path is no longer available."));
        const auto file_enrolled = agent_tasks.enroll(file_resource.resource_id, file_resource.adapter);
        if (!file_enrolled.value(QStringLiteral("ok")).toBool()) return file_enrolled;
      }
      const auto prepared_batch_reconciliation =
          agent_tasks.reconcilePreparedUnchangedTimelineDraftBatch(target_id);
      if (!prepared_batch_reconciliation.value(QStringLiteral("ok")).toBool()) {
        auto blocked = prepared_batch_reconciliation;
        blocked.insert(QStringLiteral("taskOwnershipAttached"), true);
        blocked.insert(QStringLiteral("active"), true);
        blocked.insert(QStringLiteral("targetResourceIds"), mcp_timeline_target_ids);
        return blocked;
      }
      prepared_batch_recovered_unchanged =
          prepared_batch_reconciliation.value(QStringLiteral("recoveredUnchanged")).toBool();
      recovered_prepared_step_id =
          prepared_batch_reconciliation.value(QStringLiteral("reconciledStepId"));
      auto accepted_preparation =
          timeline_document.acceptAgentTaskDocumentPreparationForAutomation();
      if (!accepted_preparation.value(QStringLiteral("ok")).toBool()) {
        const auto preparation = timeline_document.prepareAgentTaskDocumentForAutomation();
        if (!preparation.value(QStringLiteral("ok")).toBool()) {
          auto pending = preparation;
          pending.insert(QStringLiteral("retryable"), true);
          pending.insert(QStringLiteral("taskOwnershipAttached"), true);
          return pending;
        }
        if (!preparation.value(QStringLiteral("ready")).toBool()) {
          auto pending = failure(preparation.value(QStringLiteral("message")).toString());
          pending.insert(QStringLiteral("preparationPhase"), preparation.value(QStringLiteral("phase")));
          pending.insert(QStringLiteral("retryable"), true);
          pending.insert(QStringLiteral("taskOwnershipAttached"), true);
          pending.insert(QStringLiteral("preparationRetained"), true);
          return pending;
        }
        accepted_preparation = timeline_document.acceptAgentTaskDocumentPreparationForAutomation();
        if (!accepted_preparation.value(QStringLiteral("ok")).toBool()) {
          auto pending = accepted_preparation;
          pending.insert(QStringLiteral("retryable"), true);
          pending.insert(QStringLiteral("taskOwnershipAttached"), true);
          pending.insert(QStringLiteral("preparationRetained"), true);
          return pending;
        }
      }
      auto receipt = agent_tasks.state();
      receipt.insert(QStringLiteral("active"), true);
      receipt.insert(QStringLiteral("targetResourceIds"), mcp_timeline_target_ids);
      if (source_integrity_reconciled)
        receipt.insert(QStringLiteral("sourceIntegrityCheckpointReconciled"), true);
      if (compound_projection_reconciled)
        receipt.insert(QStringLiteral("compoundRecoveryProjectionVerified"), true);
      receipt.insert(QStringLiteral("recoverUnchanged"), prepared_batch_recovered_unchanged);
      if (prepared_batch_recovered_unchanged)
        receipt.insert(QStringLiteral("recoveredPreparedStepId"), recovered_prepared_step_id);
      return receipt;
    }
    if (mcp_timeline_task_id.isEmpty() || agent_tasks.journal()->taskId() != mcp_timeline_task_id ||
        args.value(QStringLiteral("taskId")).toString() != mcp_timeline_task_id)
      return failure(QStringLiteral("The requested native Timeline task is not active."));
    const auto scope = agent_tasks.journal()->nativeTimelineScope();
    const auto targets = scope.value(QStringLiteral("targetIds")).toStringList();
    const auto compound_reconciliation_allowed =
        action == QStringLiteral("revert") && timeline_compound_commands.hasUnsettledCompounds();
    if (targets.size() != 1 || targets.front() != timeline_document.agentTaskDocumentId() ||
        (!timeline_document.agentTaskDocumentReady() && !compound_reconciliation_allowed))
      return failure(QStringLiteral("The Timeline changed or is busy; the task remains available for recovery."));
    if (action == QStringLiteral("finish")) {
      const auto requested_action = args.value(QStringLiteral("action")).toString();
      if (requested_action == QStringLiteral("continue") && !edit_access())
        return failure(QStringLiteral("Continue requires an active trial or purchase."));
      if (requested_action == QStringLiteral("review"))
        return agent_tasks.finish(QStringLiteral("review"));
      const auto continued = agent_tasks.continueTask();
      if (!continued.value(QStringLiteral("ok")).toBool() &&
          continued.value(QStringLiteral("status")).toString() == QStringLiteral("source_integrity_pending") &&
          agent_tasks.state().value(QStringLiteral("status")).toString() == QStringLiteral("released")) {
        mcp_timeline_task_id.clear();
        mcp_timeline_target_ids.clear();
        timeline_document.releaseAgentTaskDocumentForAutomation();
        auto retry = continued;
        retry.insert(QStringLiteral("ownershipReleased"), true);
        retry.insert(QStringLiteral("retryAction"), QStringLiteral("timeline.recover_task"));
        return retry;
      }
      return continued;
    }
    if (action == QStringLiteral("release_reviewed")) {
      if (!args.value(QStringLiteral("confirmed")).toBool())
        return failure(QStringLiteral("Releasing Timeline ownership requires confirmed:true."));
      if (agent_tasks.state().value(QStringLiteral("status")).toString() != QStringLiteral("review"))
        return failure(QStringLiteral("Review and settle the Timeline task before releasing ownership."));
      const auto released = agent_tasks.releaseReviewedDraft();
      if (released.value(QStringLiteral("ok")).toBool() &&
          released.value(QStringLiteral("status")).toString() == QStringLiteral("released")) {
        mcp_timeline_task_id.clear();
        mcp_timeline_target_ids.clear();
        timeline_document.releaseAgentTaskDocumentForAutomation();
      }
      return released;
    }
    if (action == QStringLiteral("revert")) {
      if (!args.value(QStringLiteral("confirmed")).toBool())
        return failure(QStringLiteral("Task revert requires confirmed:true."));
      const auto compounds = timeline_compound_commands.reconcileBeforeRevert();
      if (!compounds.value(QStringLiteral("ok")).toBool()) return compounds;
      if (!timeline_document.agentTaskDocumentReady())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("retryable"), true},
                {QStringLiteral("reconciledCompounds"), true},
                {QStringLiteral("error"), QStringLiteral("Compound producers are stopped; wait for native Timeline updates to settle, then retry the confirmed revert.")}};
      const auto reverted = agent_tasks.revert();
      if (reverted.value(QStringLiteral("ok")).toBool() &&
          reverted.value(QStringLiteral("status")).toString() == QStringLiteral("reverted")) {
        mcp_timeline_task_id.clear();
        mcp_timeline_target_ids.clear();
        timeline_document.releaseAgentTaskDocumentForAutomation();
      }
      return reverted;
    }
    return failure(QStringLiteral("Unknown native Timeline task lifecycle action."));
  });
  photo_editor::desktop::TimelineDraftLifecycle timeline_draft_lifecycle(
      &timeline_document,
      [&agent_tasks, &agent_edit, &mcp_timeline_task_id]() {
        return !mcp_timeline_task_id.isEmpty() || agent_tasks.journal()->active() ||
            !agent_edit.activeTaskId().isEmpty() || agent_edit.draftActive() || agent_edit.running();
      },
      [&edit_access]() { return edit_access(); },
      [&photo_automation]() { return photo_automation.activateTimelineWorkspaceFromAutomation(); },
      qgetenv("PHOTO_EDITOR_AUTOMATION_PROJECT_INPUTS"));
  photo_automation.setTimelineDraftLifecycleHandler(
      [&timeline_draft_lifecycle](const QString& action, const QVariantMap& args) {
        return timeline_draft_lifecycle.invoke(action, args);
      });
  photo_editor::desktop::TimelinePlaybackController timeline_playback(&timeline_document);
  photo_automation.setTimelinePlaybackController(&timeline_playback);
  const auto unregistered_owner = [](const QString&, bool, bool) {
    return photo_editor::desktop::AgentAppCommandRegistry::Policy{};
  };
  // Coverage stays discoverable while each native domain receives its own
  // complete resource adapter. Reflection alone never authorizes a mutation.
  app_commands.registerObject("library", &catalog_assets, unregistered_owner);
  app_commands.registerObject("carousel", &carousel_document, unregistered_owner);
  const auto authorize_timeline_path = [&timeline_document, &agent_tasks, &agent_edit, &edit_access,
      &mcp_timeline_task_id, &mcp_timeline_target_ids](const QString& operation, const QString& path) -> QVariantMap {
    const auto fail_path = [](const QString& error) {
      return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error},
          {QStringLiteral("blockedByScope"), true}};
    };
    const auto resource_id = timeline_document.agentTaskDocumentId();
    if (!edit_access()) return fail_path(QStringLiteral("Editing requires an active trial or purchase."));
    if (mcp_timeline_task_id.isEmpty() || agent_tasks.journal()->taskId() != mcp_timeline_task_id ||
        agent_tasks.state().value(QStringLiteral("status")).toString() != QStringLiteral("running") ||
        !mcp_timeline_target_ids.contains(resource_id))
      return fail_path(QStringLiteral("The path operation requires the running task's exact Timeline target."));
    const auto scope = agent_tasks.journal()->nativeTimelineScope();
    if (scope.value(QStringLiteral("targetIds")).toStringList() != QStringList{resource_id} ||
        !timeline_document.agentTaskDocumentReady())
      return fail_path(QStringLiteral("The Timeline is busy or outside the admitted task scope."));
    const auto style_permission = agent_edit.authorizeNativeEditingCommandForTask(
        QStringLiteral("Timeline"), false,
        agent_tasks.state().value(QStringLiteral("approvedSnapshot")).toMap());
    if (!style_permission.value(QStringLiteral("ok")).toBool()) return style_permission;
    if (operation == QStringLiteral("import_media")) {
      const QFileInfo source(path);
      const auto canonical_source = source.canonicalFilePath();
      const auto allowed_paths = scope.value(QStringLiteral("assetPaths")).toStringList();
      if (!source.isFile() || !source.isReadable() || canonical_source.isEmpty() || path != canonical_source ||
          !allowed_paths.contains(canonical_source))
        return fail_path(QStringLiteral("Media import is limited to canonical source files admitted for this task."));
      return {{QStringLiteral("ok"), true}, {QStringLiteral("canonicalPath"), canonical_source}};
    }
    if (operation == QStringLiteral("save_project")) {
      const auto output_directory = scope.value(QStringLiteral("outputDirectory")).toString();
      const auto canonical_output = canonicalOutputFilePath(path);
      const QFileInfo output_file(canonical_output);
      if (canonical_output.isEmpty() || canonical_output != path ||
          !pathIsWithinDirectory(output_directory, canonical_output) || output_file.exists() ||
          !output_file.dir().exists() || !QFileInfo(output_file.dir().absolutePath()).isWritable())
        return fail_path(QStringLiteral("Project save may create only a new canonical task file inside the admitted output directory."));
      const auto file_resource = photo_editor::desktop::AgentTaskCoordinator::taskOwnedFileResource(canonical_output);
      if (file_resource.resource_id.isEmpty() || file_resource.canonical_path != canonical_output)
        return fail_path(QStringLiteral("The requested project path cannot be safely owned by this task."));
      return {{QStringLiteral("ok"), true}, {QStringLiteral("canonicalPath"), canonical_output},
          {QStringLiteral("resourceId"), file_resource.resource_id}};
    }
    return fail_path(QStringLiteral("This task does not authorize the requested path operation."));
  };
  photo_editor::desktop::register_agent_timeline_commands(app_commands, &timeline_document, agent_tasks,
      [&agent_edit, &mcp_timeline_task_id]() {
        const auto native_task_id = agent_edit.activeTaskId();
        return native_task_id.isEmpty() ? mcp_timeline_task_id : native_task_id;
      }, [&agent_edit, &timeline_document, &agent_task_target_ids, &agent_tasks,
          &mcp_timeline_task_id, &mcp_timeline_target_ids, &edit_access](const QString& name) -> QVariantMap {
        if (!edit_access())
          return {{"ok", false}, {"error", "Editing requires an active trial or purchase."}};
        const auto resource_id = timeline_document.agentTaskDocumentId();
        const auto graphics = name.contains(QStringLiteral("Title")) || name.contains(QStringLiteral("Graphic")) ||
            name.contains(QStringLiteral("Caption"));
        const auto permission_name = graphics ? QStringLiteral("TextTimeline") : QStringLiteral("Timeline");
        const auto native_task_id = agent_edit.activeTaskId();
        if (!native_task_id.isEmpty()) {
          if (!agent_task_target_ids.contains(resource_id))
            return {{"ok", false}, {"error", "Include this Timeline in the task targets before editing it."}, {"blockedByScope", true}};
          return agent_edit.authorizeNativeEditingCommand(permission_name, false);
        }
        if (mcp_timeline_task_id.isEmpty() || agent_tasks.journal()->taskId() != mcp_timeline_task_id ||
            !mcp_timeline_target_ids.contains(resource_id) ||
            agent_tasks.journal()->nativeTimelineScope().value(QStringLiteral("targetIds")).toStringList() != QStringList{resource_id})
          return {{"ok", false}, {"error", "Include this Timeline in the task targets before editing it."}, {"blockedByScope", true}};
        const auto approved_snapshot = agent_tasks.state().value(QStringLiteral("approvedSnapshot")).toMap();
        return agent_edit.authorizeNativeEditingCommandForTask(permission_name, false, approved_snapshot);
      }, authorize_timeline_path, true);
  if (!timeline_compound_commands.registerCommands(app_commands))
    qWarning() << "Task-owned Timeline compound MCP commands could not be registered.";
  app_commands.registerObject("scene", &gaussian_scene, unregistered_owner);
  app_commands.registerObject("export", &export_jobs, unregistered_owner);
  app_commands.registerObject("preferences", &app_shell, unregistered_owner);
  app_commands.registerObject("jobs", &job_queue, unregistered_owner);
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(
      "automationRequiresExplicitSessionRecovery",
      qEnvironmentVariable("PHOTO_EDITOR_ENABLE_AUTOMATION") == QStringLiteral("1"));
  auto* editor_preview_image_provider = new photo_editor::desktop::EditorPreviewImageProvider;
  editor_preview.setImageProvider(editor_preview_image_provider);
  engine.addImageProvider(QStringLiteral("editor-preview"), editor_preview_image_provider);
  const auto preview_provider_lifetime = qScopeGuard([&editor_preview] {
    editor_preview.setImageProvider(nullptr);
  });
  photo_editor::desktop::PhotoComponentWorkspaceController component_workspace(&editor_preview);
  engine.rootContext()->setContextProperty("componentWorkspace", &component_workspace);
  photo_editor::desktop::register_creative_example_thumbnail_provider(&engine);
  engine.addImageProvider(QStringLiteral("library-original"), new photo_editor::desktop::LibraryOriginalImageProvider);
  engine.addImageProvider(QStringLiteral("import-preview"), new photo_editor::desktop::ImportPreviewImageProvider);
  engine.addImageProvider(
      QStringLiteral("timeline-playback"), timeline_playback.createImageProvider());
  engine.addImageProvider(
      QStringLiteral("timeline-fractal-noise"),
      new photo_editor::desktop::timeline_motion::FractalNoiseImageProvider(
          QDir(photo_editor::desktop::AppBootstrap::cacheLocation())
              .filePath(QStringLiteral("timeline-fractal-noise"))));
  engine.rootContext()->setContextProperty(
      "timelineInspectorDeferredLoadingEnabled",
      !app.arguments().contains(QStringLiteral("--timeline-route-performance-eager-inspector")));
  engine.rootContext()->setContextProperty(
      "timelineSourceDeferredLoadingEnabled",
      !app.arguments().contains(QStringLiteral("--timeline-route-performance-eager-source-panel")));
  engine.rootContext()->setContextProperty(
      "timelineRoutePerformanceForceCollapsedInspector",
      app.arguments().contains(QStringLiteral("--timeline-route-performance-force-collapsed-inspector")));
  engine.rootContext()->setContextProperty(
      "timelineRoutePerformanceForceCollapsedSourcePanel",
      app.arguments().contains(QStringLiteral("--timeline-route-performance-force-collapsed-source-panel")));
  engine.rootContext()->setContextProperty("appShell", &app_shell);
  engine.rootContext()->setContextProperty("customerAccess", &customer_access);
  engine.rootContext()->setContextProperty("crashReporting", &crash_reporting);
  engine.rootContext()->setContextProperty("agentEdit", &agent_edit);
  engine.rootContext()->setContextProperty("agentSession", &agent_session);
  engine.rootContext()->setContextProperty("managedAgentRuntime", &managed_agent_runtime);
  engine.rootContext()->setContextProperty("gaussianScene", &gaussian_scene);
  engine.rootContext()->setContextProperty("catalogAssets", &catalog_assets);
  engine.rootContext()->setContextProperty("libraryWorkflow", &library_workflow);
  engine.rootContext()->setContextProperty("visualSearch", &visual_search);
  engine.rootContext()->setContextProperty("similaritySearch", &similarity_search);
  engine.rootContext()->setContextProperty("searchModelSetup", &search_model_setup);
  engine.rootContext()->setContextProperty("searchEdit", &search_edit);
  engine.rootContext()->setContextProperty("carouselDocument", &carousel_document);
  engine.rootContext()->setContextProperty("editorPreview", &editor_preview);
  engine.rootContext()->setContextProperty("exportJobs", &export_jobs);
  engine.rootContext()->setContextProperty("jobQueue", &job_queue);
  engine.rootContext()->setContextProperty("macosUpdater", &macos_updater);
  engine.rootContext()->setContextProperty("previewBackend", &preview_backend);
  engine.rootContext()->setContextProperty("qmlPerformance", &qml_performance);
  engine.rootContext()->setContextProperty("timelineDocument", &timeline_document);
  engine.rootContext()->setContextProperty("timelineColorEditor", &timeline_color_editor);
  engine.rootContext()->setContextProperty("timelinePlayback", &timeline_playback);

  QObject::connect(
      &engine,
      &QQmlApplicationEngine::objectCreationFailed,
      &app,
      []() { QCoreApplication::exit(EXIT_FAILURE); },
      Qt::QueuedConnection);
  engine.loadFromModule(QStringLiteral("PhotoEditor"), QStringLiteral("Main"));
  if (engine.rootObjects().isEmpty()) {
    photo_editor::desktop::observability::logWarning(
        photoEditorAppLog(), QStringLiteral("desktop.startup.qml_failed"));
    return EXIT_FAILURE;
  }
  QObject::connect(
      &app,
      &QGuiApplication::screenRemoved,
      &timeline_playback,
      [&timeline_playback](QScreen*) {
        timeline_playback.recoverRuntimeFailure(
            QStringLiteral("external-display"),
            QStringLiteral("A display disconnected; detached Timeline windows were returned to the active display."));
      });
  if (!engine.rootObjects().isEmpty()) {
    photo_automation.setQmlRootObject(engine.rootObjects().first());
    engine.rootObjects().first()->setProperty("closeCoordinator", QVariant::fromValue(&close_coordinator));
    if (auto* root_window = qobject_cast<QQuickWindow*>(engine.rootObjects().first())) {
      new photo_editor::desktop::RecentUserInputFilter(root_window);
      QObject::connect(
          root_window,
          &QQuickWindow::sceneGraphError,
          &timeline_playback,
          [&timeline_playback](QQuickWindow::SceneGraphError, const QString& message) {
            timeline_playback.recoverRuntimeFailure(QStringLiteral("gpu"), message);
      });
    }
  }
  photo_editor::desktop::AgentFullWorkflowRuntimeProof* agent_workflow_proof = nullptr;
  if (!agent_workflow_proof_dir.isEmpty()) {
    auto* window = engine.rootObjects().isEmpty() ? nullptr
        : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) return EXIT_FAILURE;
    QFile config_file(QDir(agent_workflow_proof_dir)
        .filePath(QStringLiteral("agent-workflow-private-runtime.json")));
    if (!config_file.open(QIODevice::ReadOnly)) return EXIT_FAILURE;
    const auto config = QJsonDocument::fromJson(config_file.readAll()).object();
    photo_editor::desktop::AgentFullWorkflowProofOptions options;
    options.fixture_root = agent_workflow_proof_dir;
    options.asset_id = config.value(QStringLiteral("assetId")).toString();
    options.source_path = config.value(QStringLiteral("sourcePath")).toString();
    options.installed_model_id = config.value(QStringLiteral("modelId")).toString();
    options.initial_prompt = config.value(QStringLiteral("brief")).toString();
    options.revision_prompt = config.value(QStringLiteral("revision")).toString();
    options.exact_copy = config.value(QStringLiteral("exactCopy")).toString();
    options.revision_exact_copy = config.value(QStringLiteral("revisionExactCopy")).toString();
    for (const auto& value : config.value(QStringLiteral("coverCopy")).toArray())
      options.cover_copy.append(value.toString());
    for (const auto& value : config.value(QStringLiteral("revisionCoverCopy")).toArray())
      options.revision_cover_copy.append(value.toString());
    options.minimum_output_width = config.value(QStringLiteral("minimumOutputWidth")).toInt(1600);
    options.minimum_output_height = config.value(QStringLiteral("minimumOutputHeight")).toInt(900);
    options.target_aspect_width = config.value(QStringLiteral("targetAspectWidth")).toInt(16);
    options.target_aspect_height = config.value(QStringLiteral("targetAspectHeight")).toInt(9);
    for (const auto& role : config.value(QStringLiteral("topToBottomRoles")).toArray())
      options.top_to_bottom_roles.append(role.toString());
    options.finished = [&app](const QVariantMap& receipt) {
      app.exit(receipt.value(QStringLiteral("ok")).toBool() ? EXIT_SUCCESS : EXIT_FAILURE);
    };
    agent_workflow_proof = new photo_editor::desktop::AgentFullWorkflowRuntimeProof(window,
        &agent_session, &agent_edit, &editor_preview, &catalog_assets, std::move(options), &app);
  }
  if (timeline_route_performance && !timeline_route_performance_lifecycle) {
    if (engine.rootObjects().isEmpty()) {
      return EXIT_FAILURE;
    }
    auto* root = engine.rootObjects().first();
    auto* root_window = qobject_cast<QQuickWindow*>(root);
    if (root_window != nullptr) {
      root_window->resize(1440, 900);
      root_window->setOpacity(0.0);
    }
    root->setProperty("recentUserInput", true);
    root->setProperty("qmlFrameCaptureEnabled", true);
    QMetaObject::invokeMethod(root, "beginQmlFrameCaptureWindow");

    const auto probe = std::make_shared<QVariantMap>();
    (*probe)[QStringLiteral("timeline_track_count")] = timeline_document.trackCount();
    const auto input_timer = std::make_shared<QElapsedTimer>();
    const auto route_ready_timer = std::make_shared<QElapsedTimer>();
    const auto creator_setup_flow = app.arguments().contains(
        QStringLiteral("--timeline-route-performance-creator-setup"));
    QTimer::singleShot(250, &app, [&app, root, root_window, probe, input_timer,
                                   route_ready_timer, creator_setup_flow,
                                   &timeline_document]() {
      input_timer->start();
      QTimer::singleShot(15, &app, [probe, input_timer]() {
        (*probe)[QStringLiteral("input_dispatch_delay_ms")] =
            qMax<qint64>(0, input_timer->elapsed() - 15);
      });

      const auto workspace_index = root->property("timelineWorkspaceIndex").toInt();
      QElapsedTimer route_timer;
      route_timer.start();
      root->setProperty("currentWorkspace", workspace_index);
      (*probe)[QStringLiteral("route_setter_ms")] = route_timer.elapsed();
      route_ready_timer->start();
      if (creator_setup_flow) {
        QTimer::singleShot(75, &app, [root, probe, &timeline_document]() {
          auto* timeline_view = root->findChild<QObject*>(
              QStringLiteral("timelineWorkspace"), Qt::FindChildrenRecursively);
          auto* creator_button = timeline_view == nullptr
              ? nullptr
              : timeline_view->findChild<QObject*>(
                    QStringLiteral("timelineCreatorTrackLayoutButton"),
                    Qt::FindChildrenRecursively);
          const auto creator_button_clicked = creator_button != nullptr &&
              QMetaObject::invokeMethod(creator_button, "click");
          const auto timeline_created = timeline_view != nullptr &&
              QMetaObject::invokeMethod(timeline_view, "createTimelineFromSetup");
          (*probe)[QStringLiteral("creator_setup_button_found")] = creator_button != nullptr;
          (*probe)[QStringLiteral("creator_setup_button_clicked")] = creator_button_clicked;
          (*probe)[QStringLiteral("creator_setup_timeline_created")] = timeline_created;
          (*probe)[QStringLiteral("timeline_track_count")] = timeline_document.trackCount();
        });
      }
      const auto expand_inspector = app.arguments().contains(
          QStringLiteral("--timeline-route-performance-expand-inspector"));
      const auto expand_source_panel = app.arguments().contains(
          QStringLiteral("--timeline-route-performance-expand-source-panel"));
      const auto panel_expand_timer = std::make_shared<QElapsedTimer>();
      if (expand_inspector || expand_source_panel) {
        panel_expand_timer->start();
        if (auto* timeline_view = root->findChild<QObject*>(
                QStringLiteral("timelineWorkspace"), Qt::FindChildrenRecursively)) {
          if (expand_inspector) {
            timeline_view->setProperty("effectsPanelCollapsed", false);
          }
          if (expand_source_panel) {
            timeline_view->setProperty("sourcePanelCollapsed", false);
          }
        }
        (*probe)[QStringLiteral("panel_expand_setter_ms")] = panel_expand_timer->elapsed();
        QTimer::singleShot(15, &app, [probe, panel_expand_timer]() {
          (*probe)[QStringLiteral("panel_expand_input_dispatch_delay_ms")] =
              qMax<qint64>(0, panel_expand_timer->elapsed() - 15);
        });
      }
      const int panel_settle_ms = expand_inspector || expand_source_panel ? 350 : 0;
      QTimer::singleShot(panel_settle_ms, &app, [&app, root, root_window, probe,
                                                 panel_expand_timer, expand_inspector,
                                                 expand_source_panel,
                                                 route_ready_timer,
                                                 &timeline_document]() {
        auto* editor_readiness_poll = new QTimer(&app);
        editor_readiness_poll->setInterval(25);
        QObject::connect(
            editor_readiness_poll,
            &QTimer::timeout,
            &app,
             [&app, root, root_window, probe, panel_expand_timer, expand_inspector,
             expand_source_panel, route_ready_timer, editor_readiness_poll,
             &timeline_document]() {
        auto* editor_surface = root->findChild<QObject*>(
            QStringLiteral("timelineEditorPageContent"), Qt::FindChildrenRecursively);
        auto* workspace_loading_surface = root->findChild<QObject*>(
            QStringLiteral("timelineWorkspaceLoadingSurface"), Qt::FindChildrenRecursively);
        auto* editor_loading_surface = root->findChild<QObject*>(
            QStringLiteral("timelineEditorLoadingSurface"), Qt::FindChildrenRecursively);
        const auto timeline_workspace_ready = workspace_loading_surface == nullptr
            || !workspace_loading_surface->property("visible").toBool();
        const auto timeline_editor_ready = editor_surface != nullptr
            && (editor_loading_surface == nullptr
                || !editor_loading_surface->property("visible").toBool());
        if ((!timeline_editor_ready || !timeline_workspace_ready)
            && route_ready_timer->elapsed() < 8000) {
          return;
        }
        editor_readiness_poll->stop();
        editor_readiness_poll->deleteLater();
        (*probe)[QStringLiteral("editor_surface_ready")] = timeline_editor_ready;
        (*probe)[QStringLiteral("timeline_workspace_ready")] = timeline_workspace_ready;
        (*probe)[QStringLiteral("timeline_editor_ready_ms")] = route_ready_timer->elapsed();
        QMetaObject::invokeMethod(root, "flushQmlFrameSummary");
        root->setProperty("qmlFrameCaptureEnabled", false);
        auto* timeline_view = root->findChild<QObject*>(
            QStringLiteral("timelineWorkspace"), Qt::FindChildrenRecursively);
        const auto inspector_collapsed = timeline_view != nullptr &&
            timeline_view->property("effectsPanelCollapsed").toBool();
        const auto full_inspector_loaded = timeline_view != nullptr &&
            timeline_view->findChild<QObject*>(
                QStringLiteral("timelineEffectsRail"), Qt::FindChildrenRecursively) != nullptr;
        const auto source_panel_collapsed = timeline_view != nullptr &&
            timeline_view->property("sourcePanelCollapsed").toBool();
        const auto full_source_panel_loaded = timeline_view != nullptr &&
            timeline_view->findChild<QObject*>(
                QStringLiteral("timelineSourceRail"), Qt::FindChildrenRecursively) != nullptr;
        const auto track_layout = timeline_view == nullptr
            ? QVariantMap{}
            : timeline_view->property("trackVerticalLayout").toMap();
        const auto virtualized_track_rows = timeline_view == nullptr
            ? QVariantList{}
            : timeline_view->property("timelineVirtualizedTrackRows").toList();
        const auto track_groups = timeline_document.trackGroups();
        QVariantList timeline_track_rows;
        for (const auto& value : timeline_document.timelineTracks()) {
          const auto track = value.toMap();
          timeline_track_rows.push_back(QVariantMap{
              {QStringLiteral("label"), track.value(QStringLiteral("displayLabel"))},
              {QStringLiteral("group"), track.value(QStringLiteral("groupName"))},
              {QStringLiteral("kind"), track.value(QStringLiteral("trackKind"))},
              {QStringLiteral("role"), track.value(QStringLiteral("trackRole"))},
              {QStringLiteral("targeted"), track.value(QStringLiteral("targeted"))},
          });
        }
        photo_editor::desktop::observability::logInfo(
            photoEditorPerfLog(),
            QStringLiteral("benchmark.production_timeline_route"),
            {{QStringLiteral("active_timeline"), true},
             {QStringLiteral("preview_asset_loaded"), false},
             {QStringLiteral("route_setter_ms"), probe->value(
                  QStringLiteral("route_setter_ms"))},
             {QStringLiteral("input_dispatch_delay_ms"), probe->value(
                  QStringLiteral("input_dispatch_delay_ms"))},
             {QStringLiteral("editor_surface_ready"), probe->value(
                  QStringLiteral("editor_surface_ready"))},
             {QStringLiteral("timeline_workspace_ready"), probe->value(
                  QStringLiteral("timeline_workspace_ready"))},
             {QStringLiteral("timeline_editor_ready_ms"), probe->value(
                  QStringLiteral("timeline_editor_ready_ms"))},
             {QStringLiteral("inspector_collapsed"), inspector_collapsed},
             {QStringLiteral("full_inspector_loaded"), full_inspector_loaded},
             {QStringLiteral("source_panel_collapsed"), source_panel_collapsed},
             {QStringLiteral("full_source_panel_loaded"), full_source_panel_loaded},
             {QStringLiteral("timeline_track_count"), probe->value(
                  QStringLiteral("timeline_track_count"))},
             {QStringLiteral("timeline_track_groups"), track_groups},
             {QStringLiteral("timeline_track_rows"), timeline_track_rows},
             {QStringLiteral("timeline_visible_track_row_count"), virtualized_track_rows.size()},
             {QStringLiteral("timeline_layout_hidden_rows"),
              track_layout.value(QStringLiteral("hiddenByIndex"))},
             {QStringLiteral("timeline_layout_total_height"),
              track_layout.value(QStringLiteral("totalHeight"))},
             {QStringLiteral("creator_setup_button_found"), probe->value(
                  QStringLiteral("creator_setup_button_found"))},
             {QStringLiteral("creator_setup_button_clicked"), probe->value(
                  QStringLiteral("creator_setup_button_clicked"))},
             {QStringLiteral("creator_setup_timeline_created"), probe->value(
                  QStringLiteral("creator_setup_timeline_created"))}});
        if (expand_inspector || expand_source_panel) {
          photo_editor::desktop::observability::logInfo(
              photoEditorPerfLog(),
              QStringLiteral("benchmark.timeline_panel_expand"),
              {{QStringLiteral("requested_inspector"), expand_inspector},
               {QStringLiteral("requested_source_panel"), expand_source_panel},
               {QStringLiteral("panel_expand_setter_ms"), probe->value(
                    QStringLiteral("panel_expand_setter_ms"))},
               {QStringLiteral("panel_expand_input_dispatch_delay_ms"), probe->value(
                    QStringLiteral("panel_expand_input_dispatch_delay_ms"))},
               {QStringLiteral("panel_expand_settled_ms"), panel_expand_timer->elapsed()},
               {QStringLiteral("full_inspector_loaded"), full_inspector_loaded},
               {QStringLiteral("full_source_panel_loaded"), full_source_panel_loaded}});
        }
        const auto screenshot_path = qEnvironmentVariable(
            "PHOTO_EDITOR_TIMELINE_PROBE_SCREENSHOT").trimmed();
        if (root_window != nullptr && !screenshot_path.isEmpty()) {
          root_window->grabWindow().save(screenshot_path);
        }
        app.quit();
      });
        editor_readiness_poll->start();
      });
    });
  }
  bool catalog_bootstrap_finished = false;
  if (timeline_route_performance_lifecycle) {
    if (engine.rootObjects().isEmpty()) {
      photo_editor::desktop::TimelineRoutePerformanceLifecycleProbe::
          writeCommandFailureReceipt(
              lifecycle_output_directory,
              {QStringLiteral("The packaged QML application failed to create its Main window.")});
      return EXIT_FAILURE;
    }
    auto* lifecycle_root = engine.rootObjects().first();
    auto* lifecycle_window = qobject_cast<QQuickWindow*>(lifecycle_root);
    if (lifecycle_window == nullptr) {
      photo_editor::desktop::TimelineRoutePerformanceLifecycleProbe::
          writeCommandFailureReceipt(
              lifecycle_output_directory,
              {QStringLiteral("The packaged Main root was not a native Quick window.")});
      return EXIT_FAILURE;
    }
    auto* lifecycle_probe =
        new photo_editor::desktop::TimelineRoutePerformanceLifecycleProbe(
            &app,
            lifecycle_root,
            lifecycle_window,
            &catalog_assets,
            &timeline_document,
            &timeline_playback,
            &catalog_bootstrap_finished,
            lifecycle_output_directory,
            lifecycle_source_video,
            lifecycle_source_image,
            &app);
    QTimer::singleShot(0, lifecycle_probe, [lifecycle_probe]() {
      lifecycle_probe->start();
    });
  }
  const auto placed_layer_argument = app.arguments().indexOf(QStringLiteral("--placed-layer-proof"));
  const bool placed_layer_requested = !managed_runtime_proof_dir.isEmpty() && placed_layer_argument >= 0;
  const bool export_handoff_requested = placed_layer_requested &&
      app.arguments().contains(QStringLiteral("--export-handoff-proof"));
  const bool placed_layer_recovery_requested = placed_layer_requested &&
      app.arguments().contains(QStringLiteral("--placed-layer-recovery-proof"));
  const bool placed_layer_watch_requested = placed_layer_recovery_requested &&
      app.arguments().contains(QStringLiteral("--placed-layer-watch-proof"));
  const auto placed_layer_final_stage = placed_layer_watch_requested ? 8 : placed_layer_recovery_requested ? 5 : 3;
  QString placed_layer_source_path;
  if (placed_layer_requested) {
    if (placed_layer_argument + 2 >= app.arguments().size()) return EXIT_FAILURE;
    placed_layer_source_path = app.arguments()[placed_layer_argument + 2];
    if (placed_layer_recovery_requested) {
      const auto owned_source = QDir(managed_runtime_proof_dir).filePath(
          QStringLiteral("linked-source.%1").arg(QFileInfo(placed_layer_source_path).suffix()));
      if (!QFile::copy(placed_layer_source_path, owned_source)) return EXIT_FAILURE;
      placed_layer_source_path = owned_source;
    }
    QTimer::singleShot(250, &app, [&] {
      if (!engine.rootObjects().isEmpty()) engine.rootObjects().first()->setProperty("currentWorkspace", 1);
      if (export_handoff_requested) return; // Seed after the initial catalog refresh.
      editor_preview.loadAsset(QStringLiteral("placed-layer-proof-base"), app.arguments()[placed_layer_argument + 1],
          {}, {}, {}, {}, {});
    });
  }
  auto* catalog_bootstrap_watcher = new QFutureWatcher<QString>(&app);
  // Open a supplied video in the same creation sheet as the picker/drop target.
  // Selection alone never starts an expensive reconstruction; the user confirms
  // the visible quality, destination and Build action in the workspace.
  const auto scene_video_argument = app.arguments().indexOf(QStringLiteral("--scene-video"));
  const auto scene_partial_argument = app.arguments().indexOf(QStringLiteral("--scene-partial-project"));
  const bool partial_requested = scene_partial_argument >= 0;
  const auto scene_input_argument = partial_requested ? scene_partial_argument : scene_video_argument;
  if (scene_input_argument >= 0 && scene_input_argument + 1 < app.arguments().size()) {
    const auto video = app.arguments()[scene_input_argument + 1];
    const auto frames_argument = app.arguments().indexOf(QStringLiteral("--scene-video-frames"));
    const int requested_frames = frames_argument >= 0 ? app.arguments().value(frames_argument + 1).toInt() : 0;
    const bool start_requested = app.arguments().contains(partial_requested ? QStringLiteral("--scene-partial-start") : QStringLiteral("--scene-video-start"));
    const auto capture_argument = app.arguments().indexOf(QStringLiteral("--scene-video-capture-dir"));
    QString capture_directory;
    if (capture_argument >= 0 && capture_argument + 1 < app.arguments().size()) {
      const auto requested = app.arguments()[capture_argument + 1];
      if (QFileInfo(requested).isAbsolute() && !QFileInfo::exists(requested) && QDir().mkpath(requested)) capture_directory = requested;
    }
    auto* selection_timer = new QTimer(&app);
    selection_timer->setInterval(100);
    QObject::connect(selection_timer, &QTimer::timeout, &app, [selection_timer, &engine, &gaussian_scene, video, requested_frames,
        start_requested, partial_requested, capture_directory, attempts = 0]() mutable {
      if (++attempts > 100) { selection_timer->stop(); selection_timer->deleteLater(); return; }
      if (engine.rootObjects().isEmpty()) return;
      auto* root = engine.rootObjects().first();
      root->setProperty("currentWorkspace", 5);
      if (auto* workspace = root->findChild<QObject*>("sceneWorkspace")) {
        if (partial_requested) gaussian_scene.openProject(video);
        else QMetaObject::invokeMethod(workspace, "prepareVideoInput", Q_ARG(QString, video));
        if (!partial_requested && requested_frames >= 16 && requested_frames <= 1920) {
          if (auto* frames = workspace->findChild<QObject*>("sceneVideoFrameCount")) frames->setProperty("value", requested_frames);
        }
        // Explicit development launch opt-in exercises the visible Build action,
        // not a separate backend-only route. Ordinary --scene-video only selects.
        QTimer::singleShot(350, &gaussian_scene, [window = QPointer<QQuickWindow>(qobject_cast<QQuickWindow*>(root)),
            &gaussian_scene, start_requested, partial_requested, capture_directory] {
          if (!window) return;
          if (!capture_directory.isEmpty()) window->grabWindow().save(QDir(capture_directory).filePath("selected-video.png"));
          if (start_requested) {
            auto* start = window->findChild<QObject*>(partial_requested ? "sceneBuildPartial" : "sceneStartCapture");
            if (start && start->property("enabled").toBool()) QMetaObject::invokeMethod(start, "clicked");
          }
          if (capture_directory.isEmpty()) return;
          auto* evidence = new QTimer(&gaussian_scene);
          evidence->setInterval(2000);
          QObject::connect(evidence, &QTimer::timeout, &gaussian_scene,
              [window, evidence, &gaussian_scene, capture_directory, previous = QString()]() mutable {
            if (!window) return;
            auto snapshot = gaussian_scene.snapshot();
            const auto phase = snapshot.value("state").toString();
            auto* workspace = window->findChild<QObject*>("sceneWorkspace");
            snapshot.insert("uiError", workspace ? workspace->property("actionError") : QVariant());
            snapshot.insert("windowActive", window->isActive());
            snapshot.insert("windowExposed", window->isExposed());
            snapshot.insert("applicationState", static_cast<int>(QGuiApplication::applicationState()));
            auto* viewport = window->findChild<photo_editor::desktop::GaussianSceneViewport*>("gaussianSceneViewport");
            if (viewport) snapshot.insert("viewer", viewport->status());
            QSaveFile receipt(QDir(capture_directory).filePath("ui-state.json"));
            if (receipt.open(QIODevice::WriteOnly)) { receipt.write(QJsonDocument::fromVariant(snapshot).toJson()); receipt.commit(); }
            const bool finished = !snapshot.value("busy").toBool() && (phase == "failed" || phase == "canceled" ||
                (phase == "trained" && viewport && viewport->status().value("frameAvailable").toBool()));
            auto filename = phase; filename.replace('/', '_'); filename.replace('\\', '_');
            if (phase != previous || finished) window->grabWindow().save(QDir(capture_directory).filePath(filename + ".png"));
            previous = phase;
            if (finished) { evidence->stop(); evidence->deleteLater(); }
          });
          evidence->start();
        });
        selection_timer->stop(); selection_timer->deleteLater();
      }
    });
    selection_timer->start();
  }
  if (!managed_runtime_proof_dir.isEmpty() && app.arguments().contains(QStringLiteral("--gaussian-workspace-proof"))) {
    QTimer::singleShot(250, &app, [&engine, &gaussian_scene, &app] {
      if (!engine.rootObjects().isEmpty()) engine.rootObjects().first()->setProperty("currentWorkspace", 5);
      const auto project_argument = app.arguments().indexOf(QStringLiteral("--gaussian-project-proof"));
      if (project_argument >= 0 && project_argument + 1 < app.arguments().size())
        gaussian_scene.openProject(app.arguments()[project_argument + 1]);
      const auto timed_argument = app.arguments().indexOf(QStringLiteral("--gaussian-timed-project-proof"));
      if (timed_argument >= 0 && timed_argument + 1 < app.arguments().size())
        gaussian_scene.openProject(app.arguments()[timed_argument + 1]);
      const auto spacetime_argument = app.arguments().indexOf(QStringLiteral("--gaussian-spacetime-project-proof"));
      if (spacetime_argument >= 0 && spacetime_argument + 1 < app.arguments().size())
        gaussian_scene.openProject(app.arguments()[spacetime_argument + 1]);
    });
    if (app.arguments().contains(QStringLiteral("--gaussian-timed-capture-proof"))) {
      QTimer::singleShot(750, &app, [&engine, &app] {
        const auto index = app.arguments().indexOf(QStringLiteral("--gaussian-timed-capture-proof"));
        if (index + 2 >= app.arguments().size() || engine.rootObjects().isEmpty()) { app.exit(EXIT_FAILURE); return; }
        auto* root = engine.rootObjects().first();
        auto* mode = root->findChild<QObject*>(QStringLiteral("sceneCaptureMode"));
        auto* input = root->findChild<QObject*>(QStringLiteral("sceneCapturePath"));
        auto* output = root->findChild<QObject*>(QStringLiteral("sceneProjectPath"));
        auto* start = root->findChild<QObject*>(QStringLiteral("sceneStartCapture"));
        if (!mode || !input || !output || !start) { app.exit(EXIT_FAILURE); return; }
        mode->setProperty("currentIndex", 1);
        input->setProperty("text", app.arguments()[index + 1]);
        output->setProperty("text", app.arguments()[index + 2]);
        if (!QMetaObject::invokeMethod(start, "clicked", Qt::DirectConnection)) app.exit(EXIT_FAILURE);
      });
    }
  }
  if (!managed_runtime_proof_dir.isEmpty() && app.arguments().contains(QStringLiteral("--job-history-workspace-proof"))) {
    QTimer::singleShot(250, &app, [&engine, &job_queue, &export_jobs] {
      if (!engine.rootObjects().isEmpty()) engine.rootObjects().first()->setProperty("currentWorkspace", 4);
      job_queue.refresh();
      export_jobs.initializeFromStore();
    });
  }
  QObject::connect(&job_runner, &photo_editor::desktop::JobRunner::startupRecoveryFinished,
      &app, [&export_jobs, &job_queue, &catalog_bootstrap_finished](const QString& error) {
        catalog_bootstrap_finished = true;
        const bool changed = job_queue.startupRecoveryError() != error;
        job_queue.setStartupRecoveryError(error);
        export_jobs.setStartupRecoveryError(error);
        if (error.isEmpty() || changed) {
          export_jobs.initializeFromStore();
          job_queue.refresh();
        }
      });
  QObject::connect(
      catalog_bootstrap_watcher,
      &QFutureWatcher<QString>::finished,
      &app,
      [catalog_bootstrap_watcher, &catalog_assets, &job_runner, &catalog_bootstrap_finished,
          agent_workflow_proof, &app]() {
        const auto catalog_bootstrap_error = catalog_bootstrap_watcher->result();
        if (!catalog_bootstrap_error.isEmpty()) {
          catalog_bootstrap_finished = true;
          catalog_assets.reportCatalogStartupFailure(catalog_bootstrap_error);
          photo_editor::desktop::observability::logWarning(
              photoEditorCatalogLog(),
              QStringLiteral("catalog.bootstrap.failed"),
              {{QStringLiteral("error"), catalog_bootstrap_error}});
          if (agent_workflow_proof) app.exit(EXIT_FAILURE);
        } else {
          // Catalog availability and job-dispatch readiness are independent.
          // A recovery conflict must not hide readable Library data. These
          // Library reads are independent; job history is published after the
          // recovery attempt, and the runner still gates execution.
          catalog_assets.refresh();
          job_runner.initializeFromStore();
          if (agent_workflow_proof)
            QTimer::singleShot(0, agent_workflow_proof, [agent_workflow_proof] {
              static_cast<void>(agent_workflow_proof->start());
            });
        }
        catalog_bootstrap_watcher->deleteLater();
      });
  photo_editor::desktop::observability::logInfo(
      photoEditorCatalogLog(),
      QStringLiteral("catalog.bootstrap.async_started"),
      {{QStringLiteral("elapsed_ms"), startup_timer.elapsed()}});
  catalog_bootstrap_watcher->setFuture(QtConcurrent::run([]() {
    QString catalog_bootstrap_error;
    if (!photo_editor::desktop::probe_catalog_readiness(&catalog_bootstrap_error)) {
      return catalog_bootstrap_error.isEmpty()
          ? QStringLiteral("Catalog readiness check failed.") : catalog_bootstrap_error;
    }
    return QString();
  }));
  scheduleDesktopProofAutorun(&editor_preview);

  // Periodically write session state for crash recovery
  QTimer session_timer;
  session_timer.setInterval(5000);
  QObject::connect(&session_timer, &QTimer::timeout, [&]() {
    app_shell.updateSessionState(
        catalog_assets.property("selectedAssetId").toString(),
        timeline_document.property("savedDocumentPath").toString(),
        catalog_assets.property("projectFilter").toString(),
        editor_preview.hasAsset()
            ? editor_preview
                  .currentAssetBoundAdjustmentPatchJson()
            : QString(),
        QString());
  });
  session_timer.start();

  QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
    search_model_setup.shutdown();
    session_timer.stop();
    managed_agent_runtime.stop();
    gaussian_scene.cancel();
    photo_editor::desktop::shutdown_ai_model_runtime();
    if (close_coordinator.ready()) app_shell.markCleanExit();
    photo_editor::desktop::observability::logInfo(
        photoEditorAppLog(),
        QStringLiteral("desktop.shutdown"),
        {{QStringLiteral("uptime_ms"), startup_timer.elapsed()}});
    photo_editor::desktop::observability::flush();
  });

  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("desktop.startup.ready"),
      {{QStringLiteral("elapsed_ms"), startup_timer.elapsed()}});
  QTimer runtime_proof_timer;
  QElapsedTimer runtime_proof_elapsed;
  QVariantMap runtime_ready_snapshot;
  QVariantList runtime_ready_models;
  QVariantList runtime_ready_setup_checks;
  QVariantMap runtime_ready_install_plan;
  bool runtime_proof_captured = false;
  bool runtime_proof_setup_opened = false;
  bool scene_render_panel_opened = false;
  qint64 scene_render_panel_opened_at = 0;
  QPointer<QObject> runtime_proof_setup_dialog;
  qint64 runtime_proof_previous_tick = 0;
  qint64 runtime_proof_max_tick_gap = 0;
  qint64 runtime_proof_observation_max_tick_gap = 0;
  QVariantList runtime_proof_tick_samples;
  QVariantMap runtime_proof_capture_timings;
  int runtime_proof_tick_count = 0;
  int scene_photo_proof_stage = 0;
  int scene_time_proof_stage = 0;
  int scene_timed_film_proof_stage = 0;
  double scene_time_proof_seek = 0;
  int scene_photo_proof_frame_target = 0;
  QString scene_saved_view_proof_id;
  bool scene_saved_view_proof_ok = false;
  bool scene_film_proof_ok = false;
  int placed_layer_proof_stage = 0;
  bool recipe_close_error_requested = false;
  // Native preview surfaces can update without changing the image-provider URL.
  const auto placed_layer_preview_stamp = [&editor_preview]() {
    return editor_preview.displayPreviewNativeAvailable()
        ? QStringLiteral("native:%1").arg(editor_preview.displayPreviewNativeRevision())
        : editor_preview.previewSource();
  };
  QString placed_layer_base_preview_source;
  QVariantMap placed_layer_proof_result;
  bool export_handoff_catalog_seeded = false;
  const bool scene_photo_edit_requested = app.arguments().contains(QStringLiteral("--gaussian-photo-edit-proof"));
  const bool job_recovery_isolation_requested = app.arguments().contains(QStringLiteral("--job-recovery-isolation-proof"));
  int job_recovery_isolation_stage = 0;
  QVariantMap job_recovery_isolation_result;
  const bool export_admission_requested = app.arguments().contains(QStringLiteral("--export-admission-proof"));
  int export_admission_stage = 0;
  qint64 export_admission_ms = 0;
  QString export_admission_agent_run;
  QVariantMap export_admission_result;
  int scene_photo_edit_stage = 0;
  QString scene_photo_edit_original_recipe;
  QVariantMap scene_photo_edit_result;
  int export_handoff_stage = 0;
  QString export_handoff_recipe;
  QString export_handoff_job_id;
  QString export_handoff_output;
  QVariantMap export_handoff_result;
  bool placed_layer_export_started = false;
  QElapsedTimer placed_layer_export_timer;
  qint64 placed_layer_export_last_tick = 0;
  qint64 placed_layer_export_max_tick_gap = 0;
  int placed_layer_export_tick_count = 0;
  QVariantMap scene_film_proof_result;
  QVariantMap scene_lighting_prefix_result;
  qint64 scene_lighting_render_observed_at = -1;
  const int runtime_proof_timeout = scene_photo_edit_requested ? 120000 :
      placed_layer_requested || export_admission_requested || app.arguments().contains(QStringLiteral("--gaussian-film-proof")) ? 45000 : 20000;
  QTimer scene_motion_timer;
  QElapsedTimer scene_motion_elapsed;
  int scene_motion_ticks = 0;
  qint64 scene_motion_previous = 0, scene_motion_max_gap = 0;
  QVariantList scene_motion_samples;
  if (!managed_runtime_proof_dir.isEmpty()) {
    runtime_proof_elapsed.start();
    runtime_proof_timer.setInterval(200);
    QObject::connect(&runtime_proof_timer, &QTimer::timeout, &app, [&] {
      auto* window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
      const auto now = runtime_proof_elapsed.elapsed();
      // A separate, explicitly scoped packaged Scene proof. The existing
      // managed-runtime proof below retains its agent-readiness requirements.
      if (scene_render_only_proof) {
        const bool alpha_proof = app.arguments().contains(QStringLiteral("--gaussian-alpha-model-proof"));
        const bool emitter_proof = app.arguments().contains(QStringLiteral("--gaussian-visible-emitter-proof"));
        const bool haze_proof = app.arguments().contains(QStringLiteral("--gaussian-haze-proof"));
        const bool studio_proof = app.arguments().contains(QStringLiteral("--gaussian-camera-studio-proof"));
        const bool film_controls_proof = studio_proof && app.arguments().contains(QStringLiteral("--gaussian-film-controls-proof"));
        const bool sequence_proof = app.arguments().contains(QStringLiteral("--gaussian-frame-sequence-proof"));
        const bool environment_proof = app.arguments().contains(QStringLiteral("--gaussian-environment-proof"));
        const bool tone_proof = app.arguments().contains(QStringLiteral("--gaussian-camera-tone-proof"));
        const bool autofocus_proof = app.arguments().contains(QStringLiteral("--gaussian-autofocus-proof"));
        const bool aberration_proof = app.arguments().contains(QStringLiteral("--gaussian-lens-aberration-proof"));
        const bool pupil_proof = tone_proof || autofocus_proof || aberration_proof || app.arguments().contains(QStringLiteral("--gaussian-camera-pupil-proof"));
        const bool camera_post_proof = pupil_proof || app.arguments().contains(QStringLiteral("--gaussian-camera-post-proof"));
        if (window && (window->width() != 1600 || window->height() != 900)) window->resize(1600,900);
        auto* viewport = window ? window->findChild<QObject*>(QStringLiteral("gaussianSceneViewport")) : nullptr;
        const auto state = viewport ? viewport->property("status").toMap() : QVariantMap{};
        if (scene_lighting_progressive_proof && window && window->isExposed() &&
            window->property("currentWorkspace").toInt() == window->property("sceneWorkspaceIndex").toInt()) {
          if (scene_lighting_render_observed_at < 0 && state.value("pointCount").toULongLong() > 0 && state.value("previewPhase") == "refining")
            scene_lighting_render_observed_at = now;
          const auto metrics = state.value("metrics").toMap();
          if (scene_lighting_prefix_result.isEmpty() && state.value("frameAvailable").toBool() && state.value("publishedPhase") == "refining" &&
              metrics.value("progressiveLighting").toBool() && metrics.value("softLightSamples").toInt() > 0 &&
              metrics.value("softLightSamples").toInt() < metrics.value("targetLightingSamples").toInt()) {
            const auto image = window->grabWindow();
            const bool captured = !image.isNull() && image.save(QDir(managed_runtime_proof_dir).filePath("lighting-first-prefix.png"));
            const auto since_render = scene_lighting_render_observed_at >= 0 ? now-scene_lighting_render_observed_at : -1;
            scene_lighting_prefix_result = {{"captured",captured},{"qualified",captured && since_render >= 0 && since_render <= 5000 && !metrics.value("previewReadback").toBool()},
                {"elapsedMs",now},{"sinceRenderObservedMs",since_render},{"windowExposed",window->isExposed()},{"windowActive",window->isActive()},
                {"metrics",metrics},{"scope","packaged Main visible lighting prefix after observed Scene rendering; not input-to-photon latency"}};
          }
        }
        if (now <= scene_render_wait_ms && window && state.value("frameAvailable").toBool() && state.value("previewPhase") == "photo" &&
            window->property("currentWorkspace").toInt() == window->property("sceneWorkspaceIndex").toInt()) {
          if (environment_proof && !state.value("metrics").toMap().contains("environmentLightingModel")) return;
          if (haze_proof && state.value("metrics").toMap().value("hazeModel") != "shadowed-homogeneous-single-scattering-v1") return;
          if (alpha_proof && state.value("metrics").toMap().value("alphaModel") != "inria-alpha-099-v1") return;
          if (emitter_proof && state.value("metrics").toMap().value("cameraVisibleEmitterCount").toInt() < 1) return;
          if ((camera_post_proof || emitter_proof || studio_proof) && !viewport->property("cameraPostProofRestored").toBool()) {
            const auto views = state.value("savedViews").toList();
            QVariantMap restored;
            if (!views.isEmpty() && QMetaObject::invokeMethod(viewport,"restoreView",Q_RETURN_ARG(QVariantMap,restored),
                Q_ARG(QString,views.front().toMap().value("id").toString())) && restored.value("ok").toBool()) {
              bool configured = true;
              if (pupil_proof) {
                auto photo = restored.value("camera").toMap().value("photo").toMap();
                auto lens = photo.value("lens").toMap(); lens.insert("samples",256); photo.insert("lens",lens);
                QVariantMap changed;
                configured = QMetaObject::invokeMethod(viewport,"setPhotoSettings",Q_RETURN_ARG(QVariantMap,changed),Q_ARG(QVariantMap,photo)) && changed.value("ok").toBool();
              }
              if (configured) viewport->setProperty("cameraPostProofRestored",true);
            }
            return;
          }
          if (camera_post_proof && (pupil_proof ? (state.value("lensSettings").toMap().value("schema") != "framehouse-thin-lens-v3" && state.value("lensSettings").toMap().value("schema") != "framehouse-thin-lens-v4" && state.value("lensSettings").toMap().value("schema") != "framehouse-thin-lens-v5" && state.value("lensSettings").toMap().value("schema") != "framehouse-thin-lens-v6") ||
              state.value("metrics").toMap().value("apertureSamples").toInt() != 256 : !state.value("metrics").toMap().value("cameraPostApplied").toBool())) return;
          if (aberration_proof && !state.value("lensSettings").toMap().contains("sphericalDistortion")) return;
          if (autofocus_proof && (state.value("lensSettings").toMap().value("focusMode") != "auto-centre" || state.value("autofocus").toMap().isEmpty())) return;
          if (tone_proof && !state.value("metrics").toMap().value("cameraToneApplied").toBool()) return;
          if (emitter_proof && (!state.value("metrics").toMap().value("cameraPostApplied").toBool() || state.value("autofocus").toMap().isEmpty())) return;
          auto* panel = window->findChild<QObject*>(sequence_proof ? QStringLiteral("sceneFilmExportOptions") : (camera_post_proof || studio_proof) ? QStringLiteral("scenePhotoControlsPopup") : QStringLiteral("sceneCompositionPopup"));

          if (!scene_render_panel_opened && panel) {
            const auto objects = state.value("sceneComposition").toMap().value("objects").toList();
            if (!sequence_proof && !camera_post_proof && !studio_proof && !objects.isEmpty()) panel->setProperty("selectedId",objects.front().toMap().value("id"));
            if (!sequence_proof && (environment_proof || alpha_proof || emitter_proof || haze_proof)) {
              auto* section = window->findChild<QObject*>(QStringLiteral("sceneCompositionSection"));
              const int index = haze_proof ? 3 : alpha_proof ? 2 : 1;
              if (section) { section->setProperty("currentIndex",index); QMetaObject::invokeMethod(section,"activated",Q_ARG(int,index)); }
              if (emitter_proof) {
                const auto lights = state.value("sceneComposition").toMap().value("lights").toList();
                for (const auto& value : lights) if (value.toMap().value("cameraVisible").toBool()) { panel->setProperty("selectedId",value.toMap().value("id")); break; }
              }
            }
            if (sequence_proof) {
              auto* format = window->findChild<QObject*>(QStringLiteral("sceneFilmFormatCombo"));
              auto* workspace = window->findChild<QObject*>(QStringLiteral("sceneWorkspace"));
              auto* name = window->findChild<QObject*>(QStringLiteral("sceneSequenceFolderName"));
              if (!format || !workspace || !name) return;
              format->setProperty("currentIndex",1);
              workspace->setProperty("sequenceParentFolder",QUrl::fromLocalFile(managed_runtime_proof_dir));
              name->setProperty("text",QStringLiteral("Bonsai master frames"));
            }
            if (camera_post_proof || studio_proof) {
              auto* section = window->findChild<QObject*>(QStringLiteral("scenePhotoSettingsSectionCombo"));
              auto* post_section = window->findChild<QObject*>(QStringLiteral("scenePostSectionCombo"));
              if (section) { const int index = film_controls_proof ? 3 : studio_proof ? 4 : tone_proof ? 0 : autofocus_proof ? 1 : pupil_proof ? 2 : 5; section->setProperty("currentIndex",index); QMetaObject::invokeMethod(section,"activated",Q_ARG(int,index)); }
              if (film_controls_proof) {
                auto* film_section=window->findChild<QObject*>(QStringLiteral("sceneFilmSectionCombo"));
                if (film_section) { film_section->setProperty("currentIndex",1); QMetaObject::invokeMethod(film_section,"activated",Q_ARG(int,1)); }
              }
              if (aberration_proof) {
                auto* character = window->findChild<QObject*>(QStringLiteral("sceneLensCharacterCombo"));
                if (character) { character->setProperty("currentIndex",1); QMetaObject::invokeMethod(character,"activated",Q_ARG(int,1)); }
              }
              if (post_section && !pupil_proof && !studio_proof) { post_section->setProperty("currentIndex",1); QMetaObject::invokeMethod(post_section,"activated",Q_ARG(int,1)); }
            }
            scene_render_panel_opened = QMetaObject::invokeMethod(panel,"open");
            scene_render_panel_opened_at = now;
          }
          if (scene_render_panel_opened && panel && panel->property("opened").toBool() && now-scene_render_panel_opened_at >= 400) {
            runtime_proof_timer.stop();
            const auto image = window->grabWindow();
            const bool image_ok = !image.isNull() && image.save(QDir(managed_runtime_proof_dir).filePath(film_controls_proof ? "film-studio.png" : studio_proof ? "camera-studio.png" : sequence_proof ? "frame-sequence.png" : emitter_proof ? "visible-emitters.png" : alpha_proof ? "alpha-model.png" : environment_proof ? "environment-lighting.png" : tone_proof ? "camera-tone.png" : autofocus_proof ? "camera-autofocus.png" : aberration_proof ? "camera-aberration.png" : pupil_proof ? "camera-pupil.png" : camera_post_proof ? "camera-post.png" : "scene-objects.png"));
            auto* haze_control = haze_proof ? window->findChild<QObject*>(QStringLiteral("sceneHazeShadows")) : nullptr;
            const bool haze_controls_visible = haze_control && haze_control->property("visible").toBool() && haze_control->property("checked").toBool();
            QVariantMap studio_geometry;
            bool studio_ok=true;
            if (studio_proof) {
              auto* stage=window->findChild<QQuickItem*>(QStringLiteral("sceneStudioStage"));
              const auto stage_rect=stage ? stage->mapRectToScene(stage->boundingRect()) : QRectF{};
              studio_ok=stage && stage_rect.width()>200 && stage_rect.height()>150;
              for (const auto& name:{"sceneStudioOutliner","sceneStudioInspector","sceneStudioShots"}) {
                auto* item=window->findChild<QQuickItem*>(QString::fromLatin1(name));
                const auto rect=item ? item->mapRectToScene(item->boundingRect()) : QRectF{};
                studio_ok=studio_ok && item && item->isVisible() && !stage_rect.intersects(rect);
                studio_geometry.insert(QString::fromLatin1(name),QVariantMap{{"visible",item && item->isVisible()},
                    {"x",rect.x()},{"y",rect.y()},{"width",rect.width()},{"height",rect.height()}});
              }
              QVariantMap optics;
              studio_ok=studio_ok && QMetaObject::invokeMethod(viewport,"describeCameraOptics",Q_RETURN_ARG(QVariantMap,optics),
                  Q_ARG(double,30.0)) && optics.value("ok").toBool();
              studio_geometry.insert("optics",optics);
            }
            const auto film_recipe=state.value("photoSettings").toMap().value("film").toMap();
            auto* film_slider=window->findChild<QQuickItem*>(QStringLiteral("sceneFilmHalationSlider"));
            const bool film_ok=!film_controls_proof || ((film_recipe.value("schema")=="framehouse-film-plane-v2" || film_recipe.value("schema")=="framehouse-film-plane-v3") &&
                film_recipe.value("grainAmount").toDouble()>0 && film_recipe.value("halationAmount").toDouble()>0 &&
                panel->property("page").toInt()==3 && film_slider && film_slider->isVisible());
            const bool proof_ok = image_ok && studio_ok && film_ok && (!haze_proof || haze_controls_visible) && (!scene_lighting_progressive_proof || scene_lighting_prefix_result.value("qualified").toBool());
            const QVariantMap receipt{{"ok",proof_ok}, {"scope",studio_proof ? "packaged-Main-Scene-studio-camera-and-docked-layout-only" : scene_render_offline_proof ? "packaged-Main-Scene-offline-render-and-controls-only" : sequence_proof ? "packaged-Main-Scene-PNG-sequence-render-and-format-controls-only" : emitter_proof ? "packaged-Main-Scene-visible-emitters-render-and-controls-only" : alpha_proof ? "packaged-Main-Scene-opacity-convention-render-and-controls-only" : environment_proof ? "packaged-Main-Scene-environment-render-and-controls-only" : tone_proof ? "packaged-Main-Scene-256-sample-tone-render-and-controls-only" : autofocus_proof ? "packaged-Main-Scene-256-sample-autofocus-render-and-controls-only" : aberration_proof ? "packaged-Main-Scene-256-sample-aberration-render-and-controls-only" : pupil_proof ? "packaged-Main-Scene-256-sample-pupil-render-and-controls-only" : camera_post_proof ? "packaged-Main-Scene-camera-post-render-and-controls-only" : "packaged-Main-Scene-render-and-object-controls-only"},
                {"waitLimitMs",scene_render_wait_ms}, {"timingGateQualified",!scene_render_offline_proof},
                {"lightingPrefixRequested",scene_lighting_progressive_proof},{"lightingPrefixProof",scene_lighting_prefix_result},
                {"agentRuntimeQualified",false}, {"catalogQualified",false}, {"sceneViewport",state},
                {"studioRequested",studio_proof},{"studioGeometry",studio_geometry},{"studioQualified",studio_proof && studio_ok},
                {"filmControlsRequested",film_controls_proof},{"filmControlsQualified",film_controls_proof && film_ok},
                {"environmentControlsVisible",environment_proof && panel->property("opened").toBool()},
                {"hazeControlsVisible",haze_controls_visible},
                {"alphaControlsVisible",alpha_proof && panel->property("opened").toBool()},
                {"emitterControlsVisible",emitter_proof && !sequence_proof && panel->property("opened").toBool()},
                {"sequenceControlsVisible",sequence_proof && panel->property("opened").toBool()},
                {"objectControlsVisible",!studio_proof && !camera_post_proof && !sequence_proof && panel->property("opened").toBool()}, {"cameraControlsVisible",studio_proof && panel->property("opened").toBool()}, {"cameraPostControlsVisible",camera_post_proof && !pupil_proof && panel->property("opened").toBool()}, {"pupilControlsVisible",pupil_proof && !autofocus_proof && !tone_proof && panel->property("opened").toBool()}, {"autofocusControlsVisible",autofocus_proof && panel->property("opened").toBool()}, {"toneControlsVisible",tone_proof && panel->property("opened").toBool()}, {"windowActive",window->isActive()},
                {"windowExposed",window->isExposed()}, {"width",window->width()}, {"height",window->height()},
                {"elapsedMs",now}, {"pixelWidth",image.width()}, {"pixelHeight",image.height()}};
            QFile file(QDir(managed_runtime_proof_dir).filePath("receipt.json"));
            const auto bytes = QJsonDocument::fromVariant(receipt).toJson();
            const bool saved = file.open(QIODevice::WriteOnly|QIODevice::NewOnly) && file.write(bytes) == bytes.size();
            app.exit(proof_ok && saved ? EXIT_SUCCESS : EXIT_FAILURE); return;
          }
        }
        if (now > scene_render_wait_ms) {
          runtime_proof_timer.stop();
          const QVariantMap receipt{{"ok",false}, {"scope","packaged-Main-Scene-render-and-object-controls-only"},
              {"waitLimitMs",scene_render_wait_ms}, {"timingGateQualified",false}, {"elapsedMs",now},
              {"lightingPrefixRequested",scene_lighting_progressive_proof},{"lightingPrefixProof",scene_lighting_prefix_result},
              {"windowActive",window && window->isActive()}, {"windowExposed",window && window->isExposed()},
              {"error","Scene render/control proof timed out."}, {"sceneViewport",state}, {"scene",gaussian_scene.snapshot()}};
          QFile file(QDir(managed_runtime_proof_dir).filePath("receipt.json"));
          if (file.open(QIODevice::WriteOnly|QIODevice::NewOnly)) file.write(QJsonDocument::fromVariant(receipt).toJson());
          if (window) window->grabWindow().save(QDir(managed_runtime_proof_dir).filePath("failure.png"));
          app.exit(EXIT_FAILURE);
        }
        return;
      }
      if (placed_layer_export_started && placed_layer_proof_stage == 1) {
        placed_layer_export_max_tick_gap = qMax(placed_layer_export_max_tick_gap,
            now - placed_layer_export_last_tick);
        placed_layer_export_last_tick = now;
        ++placed_layer_export_tick_count;
      }
      runtime_proof_max_tick_gap = qMax(runtime_proof_max_tick_gap, now - runtime_proof_previous_tick);
      if (!runtime_proof_captured) {
        runtime_proof_observation_max_tick_gap = qMax(
            runtime_proof_observation_max_tick_gap, now - runtime_proof_previous_tick);
      }
      runtime_proof_tick_samples.append(QVariantMap{
          {"elapsedMs", now}, {"gapMs", now - runtime_proof_previous_tick},
          {"phase", runtime_proof_captured ? "after-capture" : "observation"},
          {"windowActive", window && window->isActive()},
          {"windowExposed", window && window->isExposed()},
          {"applicationState", static_cast<int>(QGuiApplication::applicationState())}});
      runtime_proof_previous_tick = now;
      ++runtime_proof_tick_count;
      const auto snapshot = managed_agent_runtime.snapshot();
      if (!runtime_proof_captured && snapshot.value(QStringLiteral("ready")).toBool() &&
          window && window->isVisible() && catalog_bootstrap_finished && runtime_proof_elapsed.elapsed() >= 1000) {
        if (placed_layer_requested && placed_layer_proof_stage < placed_layer_final_stage) {
          if (runtime_proof_elapsed.elapsed() > runtime_proof_timeout) {
            const auto layers = editor_preview.photoLayers();
            const auto thumbnail = layers.isEmpty() ? QVariantMap{} : layers.first().toMap();
            const QVariantMap failure{{"ok", false}, {"stage", placed_layer_proof_stage},
                {"thumbnailReady", thumbnail.value("thumbnailReady")},
                {"thumbnailColorManaged", thumbnail.value("thumbnailColorManaged")},
                {"thumbnailFailureStage", thumbnail.value("thumbnailFailureStage")},
                {"thumbnailFailureDetail", thumbnail.value("thumbnailFailureDetail")},
                {"previewChanged", placed_layer_preview_stamp() != placed_layer_base_preview_source},
                {"exportStage", export_handoff_stage}, {"processing", editor_preview.processing()},
                {"hasAsset", editor_preview.hasAsset()}, {"status", editor_preview.status()},
                {"placedLayerProof", placed_layer_proof_result},
                {"exportHandoffProof", export_handoff_result}};
            QFile report(QDir(managed_runtime_proof_dir).filePath("workflow-timeout.json"));
            if (report.open(QIODevice::WriteOnly))
              report.write(QJsonDocument::fromVariant(failure).toJson(QJsonDocument::Indented));
            window->grabWindow().save(QDir(managed_runtime_proof_dir).filePath("workflow-timeout.png"));
            qCritical() << "Packaged layer workflow timed out at stage" << placed_layer_proof_stage
                        << "export stage" << export_handoff_stage << editor_preview.status();
            app.exit(EXIT_FAILURE); return;
          }
          auto* guide = window->findChild<QObject*>(QStringLiteral("firstRunGuideDialog"));
          if (guide && guide->property("visible").toBool()) {
            auto* skip = guide->findChild<QObject*>(QStringLiteral("firstRunSkipButton"));
            if (!skip || !QMetaObject::invokeMethod(skip, "clicked", Qt::DirectConnection)) {
              app.exit(EXIT_FAILURE); return;
            }
            placed_layer_proof_result.insert("firstRunGuideSkipped", true);
            return;
          }
          if (export_handoff_requested && !export_handoff_catalog_seeded) {
            if (catalog_assets.loading()) return;
            // A healthy startup refresh can clear an early in-memory fixture.
            // Seed only after it completes, then let the real QML selection load it.
            photo_editor::desktop::CatalogAssetItem item;
            item.asset_id = QStringLiteral("placed-layer-proof-base");
            item.file_path = app.arguments()[placed_layer_argument + 1];
            item.media_type = QStringLiteral("photo");
            item.online = true;
            const auto with_history = photo_editor::desktop::append_named_recipe_snapshot(
                photo_editor::desktop::EditRecipeState{}, QStringLiteral("Before handoff"),
                QStringLiteral("proof-history"));
            item.adjustment_patch_json = photo_editor::desktop::serialize_adjustment_patch_json(
                with_history, photo_editor::desktop::all_adjustment_groups(),
                photo_editor::desktop::AdjustmentPatchScope::AssetBound);
            catalog_assets.replaceAssetsForTesting({item});
            if (!catalog_assets.selectAssetById(item.asset_id, true)) { app.exit(EXIT_FAILURE); return; }
            export_handoff_catalog_seeded = true;
            return;
          }
          if (editor_preview.processing() || !editor_preview.hasAsset()) return;
          if (placed_layer_recovery_requested && placed_layer_proof_stage == 3) {
            if (placed_layer_watch_requested && editor_preview.previewFailureStage().isEmpty()) return;
            if (editor_preview.previewFailureStage() != QStringLiteral("layer_source") ||
                !editor_preview.previewFailureDetail().contains(placed_layer_source_path) ||
                placed_layer_preview_stamp() != placed_layer_proof_result.value("lastGoodPreview").toString()) {
              qCritical() << "Linked-source proof did not preserve preview and report missing media"
                          << editor_preview.previewFailureStage() << editor_preview.previewFailureDetail();
              app.exit(EXIT_FAILURE); return;
            }
            placed_layer_proof_result.insert("missingSourceFailure", editor_preview.previewFailureDetail());
            auto* failure_status = window->findChild<QObject*>(QStringLiteral("failureActivityIndicator"));
            if (!failure_status || !failure_status->property("visible").toBool()) return;
            if (failure_status->property("title").toString() != QStringLiteral("Preview failed") ||
                !failure_status->property("detail").toString().contains(placed_layer_source_path)) {
              qCritical() << "Linked-source failure status must expose its reason";
              app.exit(EXIT_FAILURE); return;
            }
            placed_layer_proof_result.insert("missingSourceStatusVisible", true);
            placed_layer_proof_result.insert("lastGoodPreviewRetained", true);
            if (!window->grabWindow().save(QDir(managed_runtime_proof_dir).filePath("missing-source.png")) ||
                !QFile::rename(placed_layer_source_path + QStringLiteral(".held"), placed_layer_source_path)) {
              app.exit(EXIT_FAILURE); return;
            }
            if (placed_layer_watch_requested) {
              placed_layer_proof_result.insert("automaticMissingSourceDetection", true);
            } else {
              editor_preview.setExposure(0.0);
            }
            placed_layer_proof_stage = 4;
            return;
          }
          if (placed_layer_watch_requested && placed_layer_proof_stage == 4 &&
              !editor_preview.previewFailureStage().isEmpty()) return;
          if (!editor_preview.previewFailureStage().isEmpty()) {
            qCritical() << "Placed-layer proof failed:" << editor_preview.previewFailureStage()
                        << editor_preview.previewFailureDetail();
            app.exit(EXIT_FAILURE); return;
          }
          auto* studio = window->findChild<QObject*>(QStringLiteral("editStudioWorkspace"));
          if (!studio) return;
          if (placed_layer_proof_stage == 0) {
            if (export_handoff_requested) {
              const auto loaded = photo_editor::desktop::parse_adjustment_patch_json(
                  editor_preview.currentAssetBoundAdjustmentPatchJson());
              if (!loaded.ok() || loaded.state->active_history_branch != "proof-history" ||
                  loaded.state->recipe_snapshots.size() != 1) {
                qCritical() << "Editor did not retain the loaded recipe history";
                app.exit(EXIT_FAILURE); return;
              }
              export_handoff_result.insert("loadedRecipeHistoryPreserved", true);
            }
            placed_layer_base_preview_source = placed_layer_preview_stamp();
            QVariant result;
            const bool invoked = QMetaObject::invokeMethod(studio, "placeSourceLayer", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, result),
                Q_ARG(QVariant, QUrl::fromLocalFile(placed_layer_source_path).toString()));
            if (!invoked || !result.toBool() || editor_preview.photoLayerCount() != 1 ||
                !studio->findChild<QObject*>(QStringLiteral("sourceLayerDialog"))) { app.exit(EXIT_FAILURE); return; }
            placed_layer_proof_result.insert("placedViaQml", true);
            placed_layer_proof_stage = 1;
            return;
          }
          if (placed_layer_proof_stage == 1) {
            if (placed_layer_preview_stamp() == placed_layer_base_preview_source) return;
            const auto thumbnail_layer = editor_preview.photoLayers().first().toMap();
            if (!thumbnail_layer.value("thumbnailReady").toBool() ||
                !thumbnail_layer.value("thumbnailColorManaged").toBool()) return;
            placed_layer_proof_result.insert("sourceThumbnailColorManaged", true);
            placed_layer_proof_result.insert("sourceThumbnailRenderMode",
                thumbnail_layer.value("thumbnailPreviewRenderMode"));
            QVariant kind_label;
            if (!QMetaObject::invokeMethod(studio, "photoLayerKindLabel", Qt::DirectConnection,
                    Q_RETURN_ARG(QVariant, kind_label), Q_ARG(QVariant, editor_preview.photoLayers().first())) ||
                kind_label.toString() != QStringLiteral("Source Image")) { app.exit(EXIT_FAILURE); return; }
            placed_layer_proof_result.insert("kindLabel", kind_label);
            const auto path = QDir(managed_runtime_proof_dir).filePath(QStringLiteral("placed-layer.png"));
            QElapsedTimer placed_operation_timer;
            placed_operation_timer.start();
            if (!placed_layer_export_started) {
              placed_layer_export_timer.start();
              if (!editor_preview.startPhotoLayerExport(0, path)) { app.exit(EXIT_FAILURE); return; }
              placed_layer_proof_result.insert("layerExportAdmissionMs", placed_operation_timer.elapsed());
              placed_layer_export_last_tick = runtime_proof_elapsed.elapsed();
              placed_layer_export_started = true;
              return;
            }
            if (editor_preview.photoExportBusy()) return;
            if (!editor_preview.photoExportError().isEmpty() || editor_preview.photoExportPath() != path) {
              qCritical() << "Async layer export failed" << editor_preview.photoExportError();
              app.exit(EXIT_FAILURE); return;
            }
            placed_layer_proof_result.insert("layerExportElapsedMs", placed_layer_export_timer.elapsed());
            placed_layer_proof_result.insert("layerExportAsync", true);
            placed_layer_proof_result.insert("layerExportMaxTickGapMs", placed_layer_export_max_tick_gap);
            placed_layer_proof_result.insert("layerExportEventLoopTicks", placed_layer_export_tick_count);
            placed_layer_proof_result.insert("layerExportPath", path);
            placed_layer_proof_result.insert("layer", editor_preview.photoLayers().first());
            const auto inspector_rows = [&] {
              auto value = studio->property("photoLayerRows");
              if (value.metaType() == QMetaType::fromType<QJSValue>())
                value = value.value<QJSValue>().toVariant();
              return value.toList();
            };
            placed_operation_timer.restart();
            editor_preview.undo();
            placed_layer_proof_result.insert("undoElapsedMs", placed_operation_timer.elapsed());
            if (editor_preview.photoLayerCount() != 0) { app.exit(EXIT_FAILURE); return; }
            if (!inspector_rows().isEmpty()) { app.exit(EXIT_FAILURE); return; }
            placed_operation_timer.restart();
            editor_preview.redo();
            placed_layer_proof_result.insert("redoElapsedMs", placed_operation_timer.elapsed());
            if (editor_preview.photoLayerCount() != 1) { app.exit(EXIT_FAILURE); return; }
            const auto restored_rows = inspector_rows();
            if (restored_rows.size() != 1 ||
                restored_rows.first().toMap().value("sourcePath") != placed_layer_source_path) {
              qCritical() << "Layer inspector snapshot did not follow undo/redo";
              app.exit(EXIT_FAILURE); return;
            }
            placed_layer_proof_result.insert("layerSnapshotUndoRedo", true);
            placed_layer_proof_result.insert("undoRedo", true);
            placed_layer_proof_stage = 2;
            return;
          }
          if (placed_layer_preview_stamp().isEmpty() ||
              placed_layer_preview_stamp() == placed_layer_base_preview_source) return;
          if (placed_layer_recovery_requested && placed_layer_proof_stage == 4 &&
              placed_layer_preview_stamp() == placed_layer_proof_result.value("lastGoodPreview").toString()) return;
          if (placed_layer_recovery_requested && placed_layer_proof_stage == 2) {
            placed_layer_proof_result.insert("lastGoodPreview", placed_layer_preview_stamp());
            if (!QFile::rename(placed_layer_source_path, placed_layer_source_path + QStringLiteral(".held"))) {
              app.exit(EXIT_FAILURE); return;
            }
            if (!placed_layer_watch_requested) editor_preview.setExposure(0.125);
            placed_layer_proof_stage = 3;
            return;
          }
          if (placed_layer_watch_requested && placed_layer_proof_stage == 4) {
            placed_layer_proof_result.insert("automaticRestorationRendered", true);
            const auto replacement = QDir(managed_runtime_proof_dir).filePath(
                QStringLiteral("relinked-source.%1").arg(QFileInfo(placed_layer_source_path).suffix()));
            if (!QFile::copy(placed_layer_source_path, replacement)) { app.exit(EXIT_FAILURE); return; }
            QVariant relinked;
            const auto layer_id = editor_preview.photoLayers().first().toMap().value("id").toString();
            if (!studio->findChild<QObject*>(QStringLiteral("relinkSourceLayerDialog")) ||
                !QMetaObject::invokeMethod(studio, "relinkSourceLayer", Qt::DirectConnection,
                    Q_RETURN_ARG(QVariant, relinked), Q_ARG(QVariant, layer_id),
                    Q_ARG(QVariant, QUrl::fromLocalFile(replacement).toString())) || !relinked.toBool()) {
              app.exit(EXIT_FAILURE); return;
            }
            placed_layer_proof_result.insert("relinkedPath", replacement);
            placed_layer_proof_result.insert("relinkViaQml", true);
            placed_layer_proof_result.insert("beforeRelinkPreview", placed_layer_preview_stamp());
            placed_layer_proof_stage = 5;
            return;
          }
          if (placed_layer_watch_requested && placed_layer_proof_stage == 5) {
            if (placed_layer_preview_stamp() == placed_layer_proof_result.value("beforeRelinkPreview").toString()) return;
            if (editor_preview.photoLayers().first().toMap().value("sourcePath") != placed_layer_proof_result.value("relinkedPath")) {
              app.exit(EXIT_FAILURE); return;
            }
            editor_preview.undo();
            if (editor_preview.photoLayers().first().toMap().value("sourcePath") != placed_layer_source_path) {
              app.exit(EXIT_FAILURE); return;
            }
            editor_preview.redo();
            if (editor_preview.photoLayers().first().toMap().value("sourcePath") != placed_layer_proof_result.value("relinkedPath")) {
              app.exit(EXIT_FAILURE); return;
            }
            placed_layer_proof_result.insert("relinkUndoRedo", true);
            placed_layer_proof_stage = 6;
            return;
          }
          if (placed_layer_watch_requested && placed_layer_proof_stage == 6) {
            auto* menu = studio->findChild<QObject*>(QStringLiteral("photoLayerPropertiesLayerActionsMenuButton"));
            if (!menu || !menu->property("visible").toBool() ||
                !QMetaObject::invokeMethod(menu, "openMenu", Qt::DirectConnection)) {
              qCritical() << "Relink proof requires the visible layer actions menu";
              app.exit(EXIT_FAILURE); return;
            }
            placed_layer_proof_stage = 7;
            return;
          }
          if (placed_layer_watch_requested && placed_layer_proof_stage == 7) {
            auto* menu = studio->findChild<QObject*>(QStringLiteral("photoLayerPropertiesLayerActionsMenuButton"));
            auto* popup = menu ? menu->findChild<QObject*>(QStringLiteral("photoLayerPropertiesLayerActionsMenuButtonPopup")) : nullptr;
            auto* content = popup ? qvariant_cast<QQuickItem*>(popup->property("contentItem")) : nullptr;
            const auto bounds = content ? content->mapRectToScene(QRectF(0, 0, content->width(), content->height())) : QRectF{};
            if (!content || !QRectF(0, 0, window->width(), window->height()).contains(bounds)) {
              qCritical() << "Layer menu must fit the viewport" << bounds;
              app.exit(EXIT_FAILURE); return;
            }
            placed_layer_proof_result.insert("relinkMenuWithinViewport", true);
            if (!window->grabWindow().save(QDir(managed_runtime_proof_dir).filePath("relink-menu.png")) ||
                !menu || !QMetaObject::invokeMethod(menu, "closeMenu", Qt::DirectConnection)) {
              app.exit(EXIT_FAILURE); return;
            }
            placed_layer_proof_result.insert("relinkMenuCaptured", true);
          }
          if (placed_layer_recovery_requested) placed_layer_proof_result.insert("restoredSourceRendered", true);
          placed_layer_proof_result.insert("previewPublished", true);
          placed_layer_proof_result.insert("ok", true);
          placed_layer_proof_stage = placed_layer_final_stage;
        }
        if (export_handoff_requested && export_handoff_stage < 4) {
          // This additional queued-export + reference-render workflow is a
          // correctness proof, not an extension of the 45s placed-layer gate.
          if (runtime_proof_elapsed.elapsed() > 120000) { app.exit(EXIT_FAILURE); return; }
          if (export_handoff_stage == 0) {
            export_handoff_recipe = editor_preview.currentAssetBoundAdjustmentPatchJson();
            window->setProperty("currentWorkspace", 4);
            if (catalog_assets.selectedAdjustmentPatchJson() != export_handoff_recipe) {
              qCritical() << "Export workspace handoff changed the asset-bound recipe";
              app.exit(EXIT_FAILURE); return;
            }
            export_handoff_result.insert("assetBoundRecipePreserved", true);
            export_handoff_stage = 1;
            return;
          }
          auto* export_view = window->findChild<QObject*>(QStringLiteral("exportStudioWorkspaceContent"));
          if (!export_view) return;
          if (export_handoff_stage == 1) {
            export_view->setProperty("deliveryDestinationPath", QDir(managed_runtime_proof_dir).filePath("delivery"));
            export_view->setProperty("deliveryPresetIndex", 0);
            if (!QMetaObject::invokeMethod(export_view, "queueCurrentSelection", Qt::DirectConnection)) {
              app.exit(EXIT_FAILURE); return;
            }
            for (const auto& record : photo_editor::desktop::loadPersistedJobRecords(nullptr, false)) {
              if (record.asset_id == editor_preview.assetId() && record.job_kind == "asset_export") {
                if (record.recipe_json != export_handoff_recipe) {
                  qCritical() << "Queued export did not capture the complete recipe";
                  app.exit(EXIT_FAILURE); return;
                }
                export_handoff_job_id = record.job_id;
                export_handoff_output = record.output_path;
              }
            }
            if (export_handoff_job_id.isEmpty()) {
              qCritical() << "Export UI did not create a persistent job" << export_jobs.status();
              app.exit(EXIT_FAILURE); return;
            }
            export_handoff_result.insert("queuedViaExportView", true);
            export_handoff_result.insert("queuedRecipePreserved", true);
            export_handoff_stage = 2;
            job_runner.wake();
            return;
          }
          if (export_handoff_stage == 2) {
            bool completed = false;
            for (const auto& value : export_jobs.jobs()) {
              const auto job = value.toMap();
              if (job.value("jobId") != export_handoff_job_id) continue;
              if (job.value("status") == "Failed") {
                qCritical() << "Queued handoff export failed" << job;
                app.exit(EXIT_FAILURE); return;
              }
              completed = job.value("status") == "Completed";
            }
            if (!completed) return;
            const auto reference_path = QDir(managed_runtime_proof_dir).filePath("handoff-reference.png");
            if (editor_preview.currentAssetBoundAdjustmentPatchJson() != export_handoff_recipe ||
                !editor_preview.startFlattenedPhotoExport(reference_path)) { app.exit(EXIT_FAILURE); return; }
            export_handoff_stage = 3;
            return;
          }
          if (editor_preview.photoExportBusy()) return;
          const QImage reference(QDir(managed_runtime_proof_dir).filePath("handoff-reference.png"));
          const QImage delivered(export_handoff_output);
          if (!editor_preview.photoExportError().isEmpty() || reference.isNull() || delivered != reference ||
              delivered.colorSpace() != reference.colorSpace()) {
            qCritical() << "Queued handoff export differs from the editor reference";
            app.exit(EXIT_FAILURE); return;
          }
          export_handoff_result.insert("pixelsAndProfileExact", true);
          export_handoff_result.insert("width", delivered.width());
          export_handoff_result.insert("height", delivered.height());
          export_handoff_result.insert("outputPath", export_handoff_output);
          export_handoff_result.insert("ok", true);
          export_handoff_stage = 4;
        }
        if (app.arguments().contains(QStringLiteral("--job-history-workspace-proof")) &&
            (job_queue.loading() || export_jobs.loading()) && runtime_proof_elapsed.elapsed() < runtime_proof_timeout) return;
        if (app.arguments().contains(QStringLiteral("--gaussian-workspace-proof")) &&
            gaussian_scene.snapshot().value(QStringLiteral("busy")).toBool() && runtime_proof_elapsed.elapsed() < runtime_proof_timeout) return;
        if (app.arguments().contains(QStringLiteral("--gaussian-timed-capture-proof")) &&
            gaussian_scene.snapshot().value("state") == "idle" && runtime_proof_elapsed.elapsed() < runtime_proof_timeout) return;
        auto* scene_viewport = window->findChild<QObject*>(QStringLiteral("gaussianSceneViewport"));
        auto viewport_status = scene_viewport ? scene_viewport->property("status").toMap() : QVariantMap{};
        if (scene_photo_edit_requested && scene_photo_edit_stage < 5) {
          if (runtime_proof_elapsed.elapsed() > runtime_proof_timeout) { app.exit(EXIT_FAILURE); return; }
          const auto rendered_path = QDir(managed_runtime_proof_dir).filePath("scene-to-edit.png");
          const auto neutral_path = QDir(managed_runtime_proof_dir).filePath("scene-edit-neutral.png");
          const auto edited_path = QDir(managed_runtime_proof_dir).filePath("scene-edit-masked.png");
          if (scene_photo_edit_stage == 0) {
            if (!viewport_status.value("frameAvailable").toBool() || catalog_assets.busy()) return;
            auto* guide = window->findChild<QObject*>(QStringLiteral("firstRunGuideDialog"));
            if (guide && guide->property("visible").toBool()) {
              auto* skip = guide->findChild<QObject*>(QStringLiteral("firstRunSkipButton"));
              if (!skip || !QMetaObject::invokeMethod(skip, "clicked", Qt::DirectConnection)) { app.exit(EXIT_FAILURE); return; }
              return;
            }
            auto* workspace = window->findChild<QObject*>("sceneWorkspace");
            auto* edge = window->findChild<QObject*>("sceneExportEdge");
            auto* button = window->findChild<QQuickItem*>("sceneEditPhotoButton");
            if (!workspace || !edge || !button || !button->isVisible() || !button->isEnabled()) { app.exit(EXIT_FAILURE); return; }
            edge->setProperty("currentIndex", 0);
            if (!window->grabWindow().save(QDir(managed_runtime_proof_dir).filePath("scene-edit-action.png"))) { app.exit(EXIT_FAILURE); return; }
            QVariant admitted;
            if (!QMetaObject::invokeMethod(workspace, "renderPhotoForEditing", Qt::DirectConnection,
                    Q_RETURN_ARG(QVariant, admitted), Q_ARG(QString, rendered_path)) ||
                !admitted.toMap().value("ok").toBool()) { app.exit(EXIT_FAILURE); return; }
            scene_photo_edit_stage = 1;
            return;
          }
          if (scene_photo_edit_stage == 1) {
            if (window->property("currentWorkspace").toInt() != 1 || editor_preview.processing() || catalog_assets.busy()) return;
            const auto imported_id = catalog_assets.assetIdForFilePath(rendered_path);
            if (imported_id.isEmpty() || editor_preview.assetId() != imported_id ||
                window->property("editorUsesLocalAsset").toBool()) {
              qCritical() << "Scene edit did not open the real imported catalog asset"; app.exit(EXIT_FAILURE); return;
            }
            scene_photo_edit_result.insert("catalogAssetId", imported_id);
            scene_photo_edit_result.insert("normalCatalogEditor", true);
            scene_photo_edit_original_recipe = editor_preview.currentAssetBoundAdjustmentPatchJson();
            if (!editor_preview.startFlattenedPhotoExport(neutral_path)) { app.exit(EXIT_FAILURE); return; }
            scene_photo_edit_stage = 2;
            return;
          }
          if (editor_preview.photoExportBusy()) return;
          if (!editor_preview.photoExportError().isEmpty()) { qCritical() << editor_preview.photoExportError(); app.exit(EXIT_FAILURE); return; }
          if (scene_photo_edit_stage == 2) {
            const QImage rendered(rendered_path), neutral(neutral_path);
            if (neutral.isNull() || rendered.convertToFormat(QImage::Format_RGBA8888) != neutral.convertToFormat(QImage::Format_RGBA8888) ||
                rendered.colorSpace() != neutral.colorSpace()) {
              qCritical() << "Scene photo changed pixels/profile on neutral editor handoff"; app.exit(EXIT_FAILURE); return;
            }
            scene_photo_edit_result.insert("neutralPixelsAndProfileExact", true);
            editor_preview.addMask("linear");
            editor_preview.setActiveMaskExposure(0.75);
            editor_preview.setOutputColorSpace("Display P3");
            if (!editor_preview.startFlattenedPhotoExport(edited_path)) { app.exit(EXIT_FAILURE); return; }
            scene_photo_edit_stage = 3;
            return;
          }
          if (scene_photo_edit_stage == 3) {
            if (editor_preview.recipeSavePending() || editor_preview.processing()) return;
            const auto outline = editor_preview.activeMaskOutlineTrace();
            if (!outline.value("outline_worker_alpha_valid").toBool() ||
                outline.value("outline_worker_alpha_hash").toString().isEmpty()) return;
            scene_photo_edit_result.insert("maskOutlineReady", true);
            scene_photo_edit_result.insert("maskOutlineAlphaHash", outline.value("outline_worker_alpha_hash"));
            if (!window->grabWindow().save(QDir(managed_runtime_proof_dir).filePath("scene-edit-mask.png"))) {
              app.exit(EXIT_FAILURE); return;
            }
            const QImage edited(edited_path), neutral(neutral_path);
            if (!editor_preview.recipeSaveError().isEmpty() || edited.isNull() || edited == neutral ||
                edited.colorSpace() != QColorSpace(QColorSpace::DisplayP3)) { app.exit(EXIT_FAILURE); return; }
            scene_photo_edit_result.insert("maskAndColorEditSaved", true);
            int undo_count = 0;
            while (editor_preview.currentAssetBoundAdjustmentPatchJson() != scene_photo_edit_original_recipe && undo_count < 8) {
              editor_preview.undo(); ++undo_count;
            }
            if (editor_preview.currentAssetBoundAdjustmentPatchJson() != scene_photo_edit_original_recipe ||
                !editor_preview.startFlattenedPhotoExport(QDir(managed_runtime_proof_dir).filePath("scene-edit-undo.png"))) { app.exit(EXIT_FAILURE); return; }
            scene_photo_edit_result.insert("undoCount", undo_count);
            scene_photo_edit_stage = 4;
            return;
          }
          if (editor_preview.recipeSavePending() || editor_preview.processing()) return;
          const QImage neutral(neutral_path), undone(QDir(managed_runtime_proof_dir).filePath("scene-edit-undo.png"));
          if (!editor_preview.recipeSaveError().isEmpty() || neutral.isNull() || undone != neutral ||
              undone.colorSpace() != neutral.colorSpace()) { app.exit(EXIT_FAILURE); return; }
          scene_photo_edit_result.insert("undoPixelsAndProfileExact", true);
          scene_photo_edit_result.insert("ok", true);
          scene_photo_edit_stage = 5;
        }
        if(app.arguments().contains("--gaussian-spacetime-project-proof")) {
          if(runtime_proof_elapsed.elapsed()>runtime_proof_timeout) { app.exit(EXIT_FAILURE); return; }
          if(!viewport_status.value("frameAvailable").toBool()) return;
          auto* slider=window->findChild<QQuickItem*>("sceneTimeSlider");
          auto* play=window->findChild<QQuickItem*>("sceneTimePlay");
          if(!slider || !play || !slider->isVisible() || !play->isVisible()) { app.exit(EXIT_FAILURE); return; }
          if(scene_time_proof_stage==0) {
            scene_time_proof_seek=viewport_status.value("timeDurationSeconds").toDouble()*0.25;
            slider->setProperty("value",scene_time_proof_seek);
            if(!QMetaObject::invokeMethod(slider,"moved",Qt::DirectConnection)) { app.exit(EXIT_FAILURE); return; }
            scene_time_proof_stage=1; return;
          }
          if(scene_time_proof_stage==1) {
            if(viewport_status.value("timePositionSeconds").toDouble()!=scene_time_proof_seek) { app.exit(EXIT_FAILURE); return; }
            if(!QMetaObject::invokeMethod(play,"clicked",Qt::DirectConnection)) { app.exit(EXIT_FAILURE); return; }
            scene_time_proof_stage=2; return;
          }
          if(scene_time_proof_stage==2) {
            if(viewport_status.value("timePositionSeconds").toDouble()<scene_time_proof_seek+0.1) return;
            if(viewport_status.value("timePlaying").toBool() && !QMetaObject::invokeMethod(play,"clicked",Qt::DirectConnection)) { app.exit(EXIT_FAILURE); return; }
            scene_time_proof_stage=3; return;
          }
        }
        if(app.arguments().contains("--gaussian-timed-film-proof")) {
          if(runtime_proof_elapsed.elapsed()>runtime_proof_timeout) { app.exit(EXIT_FAILURE); return; }
          if(!viewport_status.value("frameAvailable").toBool()) return;
          auto* workspace=window->findChild<QObject*>("sceneWorkspace");
          auto* popup=window->findChild<QObject*>("sceneTimedFilmPopup");
          auto* button=window->findChild<QObject*>("sceneTimedFilmButton");
          if(!workspace || !popup || !button) { app.exit(EXIT_FAILURE); return; }
          if(scene_timed_film_proof_stage==0) {
            auto* edge=window->findChild<QObject*>("sceneExportEdge");
            if(!edge) { app.exit(EXIT_FAILURE); return; }
            edge->setProperty("currentIndex",0);
            if(!QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection)) { app.exit(EXIT_FAILURE); return; }
            scene_timed_film_proof_stage=1; return;
          }
          if(scene_timed_film_proof_stage==1) {
            if(!popup->property("opened").toBool()) return;
            if(!window->grabWindow().save(QDir(managed_runtime_proof_dir).filePath("timed-film-options.png"))) { app.exit(EXIT_FAILURE); return; }
            QVariant result;
            if(!QMetaObject::invokeMethod(workspace,"exportTimedFilmTo",Qt::DirectConnection,Q_RETURN_ARG(QVariant,result),
                Q_ARG(QString,QDir(managed_runtime_proof_dir).filePath("timed-film.mp4"))) || !result.toMap().value("ok").toBool()) {
              qCritical()<<"Timed film UI admission failed"<<result; app.exit(EXIT_FAILURE); return;
            }
            scene_timed_film_proof_stage=2; return;
          }
          if(scene_timed_film_proof_stage==2) {
            if(viewport_status.value("filmExporting").toBool()) return;
            if(!viewport_status.value("filmResult").toMap().value("ok").toBool()) { app.exit(EXIT_FAILURE); return; }
            scene_timed_film_proof_stage=3;
          }
        }
        if (app.arguments().contains(QStringLiteral("--gaussian-project-proof")) &&
            !viewport_status.value(QStringLiteral("frameAvailable")).toBool() && runtime_proof_elapsed.elapsed() < runtime_proof_timeout) return;
        if (app.arguments().contains(QStringLiteral("--gaussian-photo-proof")) && scene_viewport) {
          if (runtime_proof_elapsed.elapsed() > runtime_proof_timeout) { app.exit(EXIT_FAILURE); return; }
          if (scene_photo_proof_stage == 0) {
            scene_photo_proof_frame_target = viewport_status.value("publishedFrames").toInt() + 1;
            scene_motion_elapsed.start();
            scene_motion_timer.setInterval(16);
            QPointer<QObject> guarded_viewport(scene_viewport);
            QPointer<QQuickWindow> guarded_window(window);
            QObject::connect(&scene_motion_timer, &QTimer::timeout, &app, [&, guarded_viewport, guarded_window] {
              const auto now = scene_motion_elapsed.elapsed();
              scene_motion_max_gap = qMax(scene_motion_max_gap, now - scene_motion_previous);
              scene_motion_samples.append(QVariantMap{{"elapsedMs", now}, {"gapMs", now - scene_motion_previous},
                  {"windowActive", guarded_window && guarded_window->isActive()},
                  {"windowExposed", guarded_window && guarded_window->isExposed()},
                  {"applicationState", static_cast<int>(QGuiApplication::applicationState())}});
              scene_motion_previous = now;
              if (!guarded_viewport) { scene_motion_timer.stop(); return; }
              QMetaObject::invokeMethod(guarded_viewport, "orbit", Qt::DirectConnection, Q_ARG(double, 0.05), Q_ARG(double, 1.0 / 60.0));
              if (++scene_motion_ticks >= 60) scene_motion_timer.stop();
            });
            scene_motion_timer.start();
            scene_photo_proof_stage = 1; return;
          }
          if (scene_photo_proof_stage == 1 || scene_photo_proof_stage == 2) {
            if (scene_motion_timer.isActive() || viewport_status.value("publishedFrames").toInt() < scene_photo_proof_frame_target ||
                viewport_status.value("exporting").toBool()) return;
            if (scene_photo_proof_stage == 2 && viewport_status.value("lastExport").toString() !=
                QDir(managed_runtime_proof_dir).filePath("novel-view.png")) { app.exit(EXIT_FAILURE); return; }
            if (scene_photo_proof_stage == 2 && app.arguments().contains(QStringLiteral("--gaussian-saved-view-proof"))) {
              auto* popup = window->findChild<QObject*>(QStringLiteral("sceneSaveViewPopup"));
              auto* button = window->findChild<QObject*>(QStringLiteral("sceneSaveViewButton"));
              auto* name = window->findChild<QObject*>(QStringLiteral("sceneViewName"));
              if (!popup || !button || !name) { app.exit(EXIT_FAILURE); return; }
              if (!popup->property("opened").toBool()) {
                QMetaObject::invokeMethod(button, "clicked", Qt::DirectConnection); return;
              }
              name->setProperty("text", QStringLiteral("Native proof shot"));
              if (!window->grabWindow().save(QDir(managed_runtime_proof_dir).filePath("save-view-dialog.png")) ||
                  !QMetaObject::invokeMethod(name, "accepted", Qt::DirectConnection)) { app.exit(EXIT_FAILURE); return; }
              scene_saved_view_proof_id = scene_viewport->property("status").toMap().value("activeViewId").toString();
              if (scene_saved_view_proof_id.isEmpty()) { app.exit(EXIT_FAILURE); return; }
              QVariantMap restored;
              QMetaObject::invokeMethod(scene_viewport, "resetCamera", Qt::DirectConnection);
              if (!QMetaObject::invokeMethod(scene_viewport, "restoreView", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, restored),
                  Q_ARG(QString, scene_saved_view_proof_id)) || !restored.value("ok").toBool()) { app.exit(EXIT_FAILURE); return; }
            }
            const auto destination = QDir(managed_runtime_proof_dir).filePath(scene_photo_proof_stage == 1 ? "novel-view.png" : "novel-view.jpg");
            QVariantMap result;
            if (!QMetaObject::invokeMethod(scene_viewport, "exportPhoto", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, result),
                Q_ARG(QString, destination), Q_ARG(int, 800)) || !result.value("ok").toBool()) { app.exit(EXIT_FAILURE); return; }
            ++scene_photo_proof_stage; return;
          }
          if (scene_photo_proof_stage == 3) {
            if (viewport_status.value("exporting").toBool()) return;
            if (viewport_status.value("lastExport").toString() != QDir(managed_runtime_proof_dir).filePath("novel-view.jpg")) { app.exit(EXIT_FAILURE); return; }
            scene_photo_proof_stage = 4;
          }
          if (app.arguments().contains(QStringLiteral("--gaussian-saved-view-proof"))) {
            if (scene_photo_proof_stage == 4) {
              // Drop the loaded scene and all transient camera state, then load the project again.
              scene_viewport->setProperty("source", QVariantMap{});
              scene_viewport->setProperty("source", gaussian_scene.snapshot());
              scene_photo_proof_stage = 5; return;
            }
            if (scene_photo_proof_stage == 5) {
              if (!viewport_status.value("frameAvailable").toBool()) return;
              QVariantMap restored, exported;
              if (!QMetaObject::invokeMethod(scene_viewport, "restoreView", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, restored),
                  Q_ARG(QString, scene_saved_view_proof_id)) || !restored.value("ok").toBool() ||
                  !QMetaObject::invokeMethod(scene_viewport, "exportPhoto", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, exported),
                  Q_ARG(QString, QDir(managed_runtime_proof_dir).filePath("reopened-view.png")), Q_ARG(int, 800)) ||
                  !exported.value("ok").toBool()) { app.exit(EXIT_FAILURE); return; }
              scene_photo_proof_stage = 6; return;
            }
            if (scene_photo_proof_stage == 6) {
              if (viewport_status.value("exporting").toBool()) return;
              const QImage first(QDir(managed_runtime_proof_dir).filePath("novel-view.png"));
              const QImage reopened(QDir(managed_runtime_proof_dir).filePath("reopened-view.png"));
              scene_saved_view_proof_ok = !first.isNull() && first == reopened &&
                  viewport_status.value("activeViewId").toString() == scene_saved_view_proof_id;
              if (!scene_saved_view_proof_ok) { app.exit(EXIT_FAILURE); return; }
              scene_photo_proof_stage = 7;
            }
          }
          if (app.arguments().contains(QStringLiteral("--gaussian-film-proof"))) {
            if (scene_photo_proof_stage == 7) {
              QVariantMap saved, path;
              QMetaObject::invokeMethod(scene_viewport, "orbit", Qt::DirectConnection, Q_ARG(double, 4.0), Q_ARG(double, 1.0));
              if (!QMetaObject::invokeMethod(scene_viewport, "saveView", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, saved),
                  Q_ARG(QString, QStringLiteral("Film end shot"))) || !saved.value("ok").toBool()) { app.exit(EXIT_FAILURE); return; }
              const QVariantMap settings{{"viewIds", QVariantList{scene_saved_view_proof_id, saved.value("id")}}, {"segmentSeconds", 1.0}, {"fps", 24}};
              if (!QMetaObject::invokeMethod(scene_viewport, "saveCameraPath", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, path),
                  Q_ARG(QVariantMap, settings)) || !path.value("ok").toBool()) { app.exit(EXIT_FAILURE); return; }
              QMetaObject::invokeMethod(scene_viewport, "playCameraPath", Qt::DirectConnection, Q_ARG(bool, true));
              scene_photo_proof_stage = 8; return;
            }
            if (scene_photo_proof_stage == 8) {
              if (viewport_status.value("pathPlaying").toBool()) return;
              if (viewport_status.value("pathSeconds").toDouble() != 1.0) { app.exit(EXIT_FAILURE); return; }
              QVariantMap exported;
              if (!QMetaObject::invokeMethod(scene_viewport, "exportFilm", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, exported),
                  Q_ARG(QString, QDir(managed_runtime_proof_dir).filePath("camera-path.mp4")), Q_ARG(int, 800)) ||
                  !exported.value("ok").toBool()) { app.exit(EXIT_FAILURE); return; }
              scene_photo_proof_stage = 9; return;
            }
            if (scene_photo_proof_stage == 9) {
              if (viewport_status.value("filmExporting").toBool()) return;
              scene_film_proof_result = viewport_status.value("filmResult").toMap();
              if (!scene_film_proof_result.value("ok").toBool()) { app.exit(EXIT_FAILURE); return; }
              scene_viewport->setProperty("source", QVariantMap{});
              scene_viewport->setProperty("source", gaussian_scene.snapshot());
              scene_photo_proof_stage = 10; return;
            }
            if (scene_photo_proof_stage == 10) {
              if (!viewport_status.value("frameAvailable").toBool()) return;
              if (viewport_status.value("cameraPath").toMap().value("viewIds").toList().size() != 2 ||
                  viewport_status.value("pathDuration").toDouble() != 1.0) { app.exit(EXIT_FAILURE); return; }
              auto* popup = window->findChild<QObject*>(QStringLiteral("sceneCameraPathPopup"));
              if (!popup || !QMetaObject::invokeMethod(popup, "open", Qt::DirectConnection)) { app.exit(EXIT_FAILURE); return; }
              scene_photo_proof_stage = 11; return;
            }
            if (scene_photo_proof_stage == 11) {
              auto* popup = window->findChild<QObject*>(QStringLiteral("sceneCameraPathPopup"));
              if (!popup || !popup->property("opened").toBool()) return;
              if (!window->grabWindow().save(QDir(managed_runtime_proof_dir).filePath("camera-path-dialog.png"))) { app.exit(EXIT_FAILURE); return; }
              QMetaObject::invokeMethod(popup, "close", Qt::DirectConnection);
              QMetaObject::invokeMethod(scene_viewport, "seekCameraPath", Qt::DirectConnection, Q_ARG(double, .5));
              scene_photo_proof_frame_target = viewport_status.value("publishedFrames").toInt() + 1;
              scene_photo_proof_stage = 12; return;
            }
            if (scene_photo_proof_stage == 12) {
              if (viewport_status.value("publishedFrames").toInt() < scene_photo_proof_frame_target) return;
              scene_film_proof_ok = true; scene_photo_proof_stage = 13;
            }
          }
        }
        if (app.arguments().contains(QStringLiteral("--managed-runtime-setup-proof")) && !runtime_proof_setup_opened) {
          auto* dialog = window->findChild<QObject*>(QStringLiteral("agentModelSetupDialog"));
          if (!dialog || !QMetaObject::invokeMethod(dialog, "present", Qt::DirectConnection)) {
            app.exit(EXIT_FAILURE);
            return;
          }
          runtime_proof_setup_opened = true;
          runtime_proof_setup_dialog = dialog;
          return;
        }
        if (runtime_proof_setup_dialog && !runtime_proof_setup_dialog->property("opened").toBool()) {
          if (runtime_proof_elapsed.elapsed() < runtime_proof_timeout) return;
          app.exit(EXIT_FAILURE);
          return;
        }
        if (app.arguments().contains(QStringLiteral("--recipe-close-error-proof"))) {
          if (!recipe_close_error_requested) {
            recipe_close_error_requested = true;
            if (window->close()) { app.exit(EXIT_FAILURE); return; }
            return;
          }
          auto* dialog = window->findChild<QObject*>(QStringLiteral("editorCloseDialog"));
          if (!window->isVisible() || close_coordinator.error().isEmpty() || close_coordinator.ready()) {
            app.exit(EXIT_FAILURE); return;
          }
          if (!dialog || !dialog->property("opened").toBool()) return;
        }
        if (export_admission_requested) {
          if (runtime_proof_elapsed.elapsed() > runtime_proof_timeout) {
            qWarning() << "Export admission proof timed out at stage" << export_admission_stage << export_jobs.status();
            app.exit(EXIT_FAILURE); return;
          }
          if (catalog_assets.loading() || !catalog_bootstrap_finished) return;
          if (catalog_assets.rowCount() < 1 || !catalog_assets.errorMessage().isEmpty() ||
              !job_queue.startupRecoveryError().isEmpty()) { app.exit(EXIT_FAILURE); return; }
          auto* guide = window->findChild<QObject*>(QStringLiteral("firstRunGuideDialog"));
          if (guide && guide->property("visible").toBool()) {
            auto* skip = guide->findChild<QObject*>(QStringLiteral("firstRunSkipButton"));
            if (!skip || !QMetaObject::invokeMethod(skip, "clicked", Qt::DirectConnection)) { app.exit(EXIT_FAILURE); return; }
            return;
          }
          if (export_admission_stage == 0) {
            catalog_assets.selectIndex(0);
            catalog_assets.selectAll();
            window->setProperty("currentWorkspace", 4);
            export_admission_stage = 1;
            return;
          }
          auto* panel = window->findChild<QQuickItem*>(QStringLiteral("exportQueuePanel"));
          if (!panel || !panel->isVisible() || window->property("workspaceTransitionActive").toBool()) return;
          const auto source = catalog_assets.selectedFilePath();
          const auto destination = QDir(managed_runtime_proof_dir).filePath("exports");
          if (export_admission_stage == 1) {
            for (int i = 0; i < 8; ++i) {
              QElapsedTimer admission_timer; admission_timer.start();
              if (!export_jobs.queueExport(catalog_assets.selectedAssetId(), "photo", QStringLiteral("Admission %1").arg(i),
                  source, {}, {}, "Original Copy", destination)) { app.exit(EXIT_FAILURE); return; }
              if (i == 0) export_admission_ms = admission_timer.elapsed();
            }
            if (export_admission_ms >= 50 || export_jobs.pendingAdmissionCount() < 1) { app.exit(EXIT_FAILURE); return; }
            export_admission_stage = 2;
            return;
          }
          if (export_admission_stage == 2) {
            if (export_jobs.pendingAdmissionCount() < 1 || !panel->property("subtitle").toString().contains("Saving") ||
                !window->grabWindow().save(QDir(managed_runtime_proof_dir).filePath("export-admission-saving.png")) ||
                window->close()) { app.exit(EXIT_FAILURE); return; }
            export_admission_stage = 3;
            return;
          }
          if (export_admission_stage == 3) {
            auto* dialog = window->findChild<QObject*>(QStringLiteral("exportAdmissionCloseDialog"));
            if (!dialog || !dialog->property("opened").toBool() || !window->isVisible() ||
                !window->grabWindow().save(QDir(managed_runtime_proof_dir).filePath("export-admission-close.png"))) {
              app.exit(EXIT_FAILURE); return;
            }
            QMetaObject::invokeMethod(dialog, "close", Qt::DirectConnection);
            const auto submitted = agent_edit.queueExportForSelected("Original Copy", destination, true, {});
            if (!submitted.value("admissionPending").toBool() || submitted.value("durablyQueued").toBool() ||
                submitted.value("queuedCount").toInt() != 0 || submitted.value("submittedCount").toInt() != 1) {
              qWarning() << "Agent admission proof failed" << submitted;
              app.exit(EXIT_FAILURE); return;
            }
            export_admission_agent_run = submitted.value("runId").toString();
            export_admission_stage = 4;
            return;
          }
          if (export_jobs.pendingAdmissionCount() > 0 || export_jobs.loading() || agent_edit.running()) return;
          const auto report = agent_edit.lastRunReport();
          if (!export_jobs.admissionError().isEmpty() || report.value("runId") != export_admission_agent_run ||
              !report.value("durablyQueued").toBool() || report.value("queuedCount").toInt() != 1) {
            app.exit(EXIT_FAILURE); return;
          }
          if (export_jobs.jobCount() != 9) return;
          QFile original(source);
          if (!original.open(QIODevice::ReadOnly)) { app.exit(EXIT_FAILURE); return; }
          const auto original_bytes = original.readAll();
          for (const auto& value : export_jobs.jobs()) {
            const auto job = value.toMap();
            if (job.value("status") == "Failed") { app.exit(EXIT_FAILURE); return; }
            if (job.value("status") != "Completed") return;
            QFile output(job.value("outputPath").toString());
            if (!output.open(QIODevice::ReadOnly) || output.readAll() != original_bytes) { app.exit(EXIT_FAILURE); return; }
          }
          export_admission_result = {{"ok", true}, {"normalAdmissionMs", export_admission_ms},
              {"savingVisible", true}, {"closeGuardVisible", true}, {"agentSubmissionPending", true},
              {"agentDurableReceipt", true}, {"jobCount", 9}, {"allOriginalCopiesExact", true}};
        }
        if (job_recovery_isolation_requested) {
          if ((catalog_assets.loading() || job_queue.loading() || export_jobs.loading() || job_queue.startupRecoveryError().isEmpty()) &&
              runtime_proof_elapsed.elapsed() < runtime_proof_timeout) return;
          auto* title = window->findChild<QObject*>(QStringLiteral("recoverySummaryTitle"));
          auto* guide = window->findChild<QObject*>(QStringLiteral("firstRunGuideDialog"));
          if (((guide && guide->property("visible").toBool()) || window->property("workspaceTransitionActive").toBool()) &&
              runtime_proof_elapsed.elapsed() < runtime_proof_timeout) return;
          auto* done = window->findChild<QQuickItem*>(QStringLiteral("recoveryDoneButton"));
          auto* repair = window->findChild<QQuickItem*>(QStringLiteral("catalogRepairButton"));
          const bool isolated = catalog_assets.rowCount() > 0 && catalog_assets.errorMessage().isEmpty() &&
              !job_queue.startupRecoveryError().isEmpty() && window->property("jobRecoveryNeeded").toBool() &&
              !window->property("catalogRecoveryNeeded").toBool() &&
              title && title->property("text").toString() == QStringLiteral("Background jobs paused") &&
              app_shell.firstRunExperiencePending() &&
              (!guide || !guide->property("visible").toBool()) && !window->property("workspaceTransitionActive").toBool() &&
              done && !done->isVisible() && repair && !repair->isVisible();
          if (!isolated) { app.exit(EXIT_FAILURE); return; }
          if (job_recovery_isolation_stage == 0) {
            if (window->property("currentWorkspace").toInt() != 0 ||
                !window->grabWindow().save(QDir(managed_runtime_proof_dir).filePath("job-recovery-library.png"))) {
              app.exit(EXIT_FAILURE); return;
            }
            auto* review = window->findChild<QObject*>(QStringLiteral("recoveryReviewWorkButton"));
            if (!review || !QMetaObject::invokeMethod(review, "clicked", Qt::DirectConnection)) { app.exit(EXIT_FAILURE); return; }
            job_recovery_isolation_stage = 1;
            return;
          }
          auto* panel = window->findChild<QQuickItem*>(QStringLiteral("exportQueuePanel"));
          if ((!panel || !panel->isVisible() || window->property("currentWorkspace").toInt() != 4) &&
              runtime_proof_elapsed.elapsed() < runtime_proof_timeout) return;
          if (!panel || !panel->isVisible() || window->property("currentWorkspace").toInt() != 4) { app.exit(EXIT_FAILURE); return; }
          auto* clear_done = window->findChild<QQuickItem*>(QStringLiteral("exportQueueManagementMenuButton"));
          const auto before_jobs = job_queue.jobsForCenter();
          const auto before_exports = export_jobs.jobs();
          if (!clear_done || clear_done->property("enabled").toBool() || job_queue.historyMutationsEnabled() ||
              export_jobs.historyMutationsEnabled() || before_jobs.isEmpty() || before_exports.isEmpty()) {
            qWarning() << "Recovery control gate failed" << clear_done
                       << (clear_done ? clear_done->property("enabled") : QVariant{})
                       << job_queue.historyMutationsEnabled() << export_jobs.historyMutationsEnabled()
                       << before_jobs.size() << before_exports.size();
            app.exit(EXIT_FAILURE); return;
          }
          // Invoke the controllers directly as well as checking disabled UI:
          // stale menus, shortcuts and agent callers must not bypass recovery.
          for (const auto& value : before_jobs) {
            const auto id = value.toMap().value("jobId").toString();
            if (job_queue.retryJob(id) || job_queue.cancelJob(id) || job_queue.pauseJob(id) ||
                job_queue.resumeJob(id) || job_queue.setJobPriority(id, "high") ||
                job_queue.setRenderResourcePolicy(id, "balanced") || job_queue.moveRenderJob(id, -1) ||
                !job_queue.duplicateRenderJob(id).isEmpty()) { app.exit(EXIT_FAILURE); return; }
            job_queue.removeJob(id);
          }
          job_queue.retryFailedJobs();
          job_queue.clearFinishedJobs();
          job_queue.acknowledgeRecoveryReport();
          for (const auto& value : before_exports) {
            const auto id = value.toMap().value("jobId").toString();
            export_jobs.retryJob(id);
            export_jobs.removeJob(id);
          }
          export_jobs.clearFinishedJobs();
          if (job_queue.jobsForCenter() != before_jobs || export_jobs.jobs() != before_exports) {
            app.exit(EXIT_FAILURE); return;
          }
          job_recovery_isolation_result = {{"ok", true}, {"assetCount", catalog_assets.rowCount()},
              {"jobCount", job_queue.jobCount()}, {"error", job_queue.startupRecoveryError()},
              {"libraryAvailable", true}, {"catalogRepairHidden", true}, {"dismissHidden", true},
              {"reviewJobsOpened", true}, {"onboardingHidden", true}, {"onboardingStillPending", true},
              {"navigationSettled", true}, {"historyMutationsBlocked", true}, {"clearDoneDisabled", true}};
        }
        QElapsedTimer capture_stage_timer;
        capture_stage_timer.start();
        if (!window->grabWindow().save(QDir(managed_runtime_proof_dir).filePath(QStringLiteral("startup.png")))) {
          app.exit(EXIT_FAILURE);
          return;
        }
        runtime_proof_capture_timings.insert("screenshotMs", capture_stage_timer.restart());
        runtime_ready_snapshot = snapshot;
        runtime_ready_models = agent_edit.availableModels();
        runtime_proof_capture_timings.insert("modelsSnapshotMs", capture_stage_timer.restart());
        runtime_ready_install_plan = agent_edit.prepareSelectedModelInstall();
        runtime_proof_capture_timings.insert("modelInstallPlanMs", capture_stage_timer.restart());
        runtime_ready_setup_checks = agent_edit.setupLocalAi(false).value(QStringLiteral("checks")).toList();
        runtime_proof_capture_timings.insert("localAiSetupChecksMs", capture_stage_timer.restart());
        runtime_proof_captured = true;
        managed_agent_runtime.stop();
        runtime_proof_capture_timings.insert("runtimeStopRequestMs", capture_stage_timer.elapsed());
      }
      const bool stopped = snapshot.value(QStringLiteral("state")).toString() == QStringLiteral("stopped");
      const bool timed_out = runtime_proof_elapsed.elapsed() >
          (export_handoff_requested ? 120000 : runtime_proof_timeout);
      const bool failed = snapshot.value(QStringLiteral("state")).toString() == QStringLiteral("failed");
      if ((runtime_proof_captured && stopped) || timed_out || failed) {
        runtime_proof_timer.stop();
        const bool scene_requested = app.arguments().contains(QStringLiteral("--gaussian-workspace-proof"));
        const bool scene_loaded = window &&
            (window->property("currentWorkspace").toInt() == 5 || (scene_photo_edit_requested && scene_photo_edit_stage == 5)) &&
            window->findChild<QObject*>(QStringLiteral("sceneWorkspace")) != nullptr;
        const bool scene_project_requested = app.arguments().contains(QStringLiteral("--gaussian-project-proof"));
        const bool spacetime_requested = app.arguments().contains("--gaussian-spacetime-project-proof");
        auto* scene_viewport = window ? window->findChild<QObject*>(QStringLiteral("gaussianSceneViewport")) : nullptr;
        const auto viewport_status = scene_viewport ? scene_viewport->property("status").toMap() : QVariantMap{};
        auto* recovery_surface = window ? window->findChild<QQuickItem*>(QStringLiteral("recoverySummarySurface")) : nullptr;
        auto* workspace_surface = window ? window->findChild<QQuickItem*>(QStringLiteral("mainWorkspaceSurface")) : nullptr;
        const bool recovery_controls_unobscured = recovery_surface && workspace_surface &&
            (!recovery_surface->isVisible() || recovery_surface->mapToScene(QPointF(0, recovery_surface->height())).y() <=
                workspace_surface->mapToScene(QPointF()).y());
        auto scene_snapshot = gaussian_scene.snapshot();
        const auto timed_capture = scene_snapshot.value("capture").toMap();
        scene_snapshot.remove(QStringLiteral("capture"));
        scene_snapshot.remove(QStringLiteral("logTail"));
        const bool timed_requested = app.arguments().contains("--gaussian-timed-project-proof") ||
            app.arguments().contains("--gaussian-timed-capture-proof");
        auto* timed_counts = window ? window->findChild<QQuickItem*>(QStringLiteral("sceneCaptureCounts")) : nullptr;
        const bool timed_ok = !timed_requested || (scene_snapshot.value("state") == "prepared-not-trained" &&
            !scene_snapshot.value("busy").toBool() && !scene_snapshot.value("trainingAvailable").toBool() &&
            timed_capture.value("imagesVerified").toBool() && !viewport_status.value("frameAvailable").toBool() &&
            timed_counts && timed_counts->isVisible() && timed_counts->property("text").toString().contains("capture times"));
        const bool spacetime_ok = !spacetime_requested || (scene_time_proof_stage==3 && scene_snapshot.value("state")=="artifact-ready" &&
            viewport_status.value("temporal").toBool() && viewport_status.value("frameAvailable").toBool() &&
            !viewport_status.value("timePlaying").toBool() && viewport_status.value("timePositionSeconds").toDouble()>scene_time_proof_seek);
        const bool scene_ok = !scene_requested || (scene_loaded && recovery_controls_unobscured && timed_ok && spacetime_ok && (!scene_project_requested ||
            (scene_snapshot.value(QStringLiteral("state")).toString() == QStringLiteral("trained") &&
             viewport_status.value(QStringLiteral("frameAvailable")).toBool())));
        const bool history_requested = app.arguments().contains(QStringLiteral("--job-history-workspace-proof"));
        const auto* history_title = window ? window->findChild<QObject*>(QStringLiteral("jobHistoryEmptyTitle")) : nullptr;
        const auto history_title_text = history_title ? history_title->property("text").toString() : QString{};
        const bool history_ok = !history_requested || (!job_queue.loading() && !job_queue.historyError().isEmpty() &&
            !export_jobs.loading() && !export_jobs.historyError().isEmpty() &&
            history_title_text == QStringLiteral("Job history unavailable"));
        const bool saved_views_ok = !app.arguments().contains(QStringLiteral("--gaussian-saved-view-proof")) || scene_saved_view_proof_ok;
        const bool film_ok = !app.arguments().contains(QStringLiteral("--gaussian-film-proof")) || scene_film_proof_ok;
        const bool timed_film_ok = !app.arguments().contains("--gaussian-timed-film-proof") || scene_timed_film_proof_stage==3;
        const bool placed_layer_ok = !placed_layer_requested || placed_layer_proof_result.value("ok").toBool();
        auto* save_chip = window ? window->findChild<QQuickItem*>(QStringLiteral("recipeSaveStatusChip")) : nullptr;
        const bool save_error_visible = save_chip && save_chip->isVisible() &&
            save_chip->property("text") == QStringLiteral("Edits not safely saved") &&
            save_chip->property("tooltipText") == editor_preview.recipeSaveError() &&
            !editor_preview.recipeSaveError().isEmpty();
        const bool save_proof_ok = !recipe_save_error_proof || (save_error_visible && editor_preview.recipeSavePending());
        const bool ok = runtime_proof_captured && stopped && !snapshot.value(QStringLiteral("ownsProcess")).toBool() && scene_ok && history_ok && saved_views_ok && film_ok && timed_film_ok && placed_layer_ok && save_proof_ok &&
            (!export_handoff_requested || (export_handoff_result.value("ok").toBool() && runtime_proof_elapsed.elapsed() <= 120000)) &&
            (!scene_photo_edit_requested || scene_photo_edit_result.value("ok").toBool()) &&
            (!job_recovery_isolation_requested || job_recovery_isolation_result.value("ok").toBool()) &&
            (!export_admission_requested || export_admission_result.value("ok").toBool());
        const QVariantMap receipt{{"ok", ok}, {"readySnapshot", runtime_ready_snapshot},
            {"modelsAtReady", runtime_ready_models}, {"setupChecksAtReady", runtime_ready_setup_checks},
            {"modelSetupShown", runtime_proof_setup_opened}, {"modelInstallPlan", runtime_ready_install_plan},
            {"catalogStatus", catalog_assets.status()}, {"catalogError", catalog_assets.errorMessage()},
            {"catalogLoading", catalog_assets.loading()}, {"eventLoopTicks", runtime_proof_tick_count},
            {"jobHistoryProof", history_ok}, {"jobHistoryLoading", job_queue.loading()},
            {"jobHistoryError", job_queue.historyError()}, {"jobHistoryTitle", history_title_text},
            {"jobRecoveryIsolationProof", job_recovery_isolation_result},
            {"exportAdmissionProof", export_admission_result},
            {"exportHistoryLoading", export_jobs.loading()}, {"exportHistoryError", export_jobs.historyError()},
            {"exportHistoryStatus", export_jobs.status()},
            {"maxEventLoopTickGapMs", runtime_proof_max_tick_gap}, {"elapsedMs", runtime_proof_elapsed.elapsed()},
            {"maxObservationTickGapMs", runtime_proof_observation_max_tick_gap},
            {"eventLoopTickSamples", runtime_proof_tick_samples},
            {"proofCaptureTimings", runtime_proof_capture_timings},
            {"timingScope", "post-QML proof observation and capture; not input-to-photon"},
            {"finalSnapshot", snapshot}, {"packagedQmlLoaded", window != nullptr},
            {"sceneWorkspaceLoaded", scene_loaded}, {"sceneSnapshot", scene_snapshot},
            {"timedCaptureProof", QVariantMap{{"requested", timed_requested}, {"ok", timed_ok}, {"capture", timed_capture},
                {"countsText", timed_counts ? timed_counts->property("text") : QVariant{}}}},
            {"sceneViewport", viewport_status}, {"scenePhotoProofStage", scene_photo_proof_stage},
            {"spacetimeWorkspaceProof",spacetime_ok},{"spacetimeProofRequested",spacetime_requested},{"spacetimeProofStage",scene_time_proof_stage},
            {"timedFilmProof",timed_film_ok},{"timedFilmProofRequested",app.arguments().contains("--gaussian-timed-film-proof")},{"timedFilmProofStage",scene_timed_film_proof_stage},
            {"sceneSavedViewProof", scene_saved_view_proof_ok},
            {"sceneFilmProof", scene_film_proof_ok}, {"sceneFilmResult", scene_film_proof_result},
            {"placedLayerProof", placed_layer_proof_result},
            {"exportHandoffProof", export_handoff_result},
            {"scenePhotoEditProof", scene_photo_edit_result},
            {"recipeSaveProof", QVariantMap{{"requested", recipe_save_error_proof}, {"ok", save_proof_ok},
                {"errorVisible", save_error_visible}, {"error", editor_preview.recipeSaveError()},
                {"pending", editor_preview.recipeSavePending()}}},
            {"closeBlockedProof", QVariantMap{{"requested", recipe_close_error_requested},
                {"blocked", recipe_close_error_requested && !close_coordinator.ready() && !close_coordinator.error().isEmpty()},
                {"error", close_coordinator.error()}}},
            {"sceneMotionTicks", scene_motion_ticks}, {"sceneMotionElapsedMs", scene_motion_previous},
            {"sceneMotionMaxTimerGapMs", scene_motion_max_gap},
            {"sceneMotionSamples", scene_motion_samples},
            {"sceneInteractionProof", photo_editor::desktop::gaussian_interaction_proof(scene_motion_samples)},
            {"recoveryControlsUnobscured", recovery_controls_unobscured},
            {"selectedModelId", agent_edit.selectedModelId()}, {"models", agent_edit.availableModels()},
            {"modelWeightsDownloaded", false}, {"catalogIsolated", true}};
        QFile output(QDir(managed_runtime_proof_dir).filePath(QStringLiteral("receipt.json")));
        if (!output.open(QIODevice::WriteOnly) ||
            output.write(QJsonDocument::fromVariant(receipt).toJson()) < 0) {
          app.exit(EXIT_FAILURE);
          return;
        }
        output.close();
        if (ok && app.arguments().contains(QStringLiteral("--recipe-close-proof"))) {
          const auto write_close_receipt = [&, receipt](bool passed) {
            auto final_receipt = receipt;
            const bool stopped_helper = editor_preview.recipeSaveProcessForClose()->state() == QProcess::NotRunning;
            final_receipt.insert("ok", passed && close_coordinator.ready() && stopped_helper);
            final_receipt.insert("coordinatedCloseProof", QVariantMap{{"ready", close_coordinator.ready()},
                {"helperStopped", stopped_helper}, {"error", close_coordinator.error()},
                {"pendingRecovery", editor_preview.recipeSavePending()}});
            QSaveFile file(QDir(managed_runtime_proof_dir).filePath(QStringLiteral("receipt.json")));
            const auto bytes = QJsonDocument::fromVariant(final_receipt).toJson();
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
          };
          QObject::connect(&close_coordinator, &photo_editor::desktop::EditorCloseCoordinator::closeReady, &app,
              [&, write_close_receipt] { if (!write_close_receipt(true)) app.exit(EXIT_FAILURE); });
          QTimer::singleShot(5000, &app, [&, write_close_receipt] {
            write_close_receipt(false);
            app.exit(EXIT_FAILURE);
          });
          if (!window || window->close()) app.exit(EXIT_FAILURE);
          return;
        }
        app.exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
      }
    });
    runtime_proof_timer.start();
  }
  const auto qml_root_lifetime = qScopeGuard([&engine] {
    // Views release their controller references before component sessions and
    // their image sinks are destroyed during application shutdown.
    const auto roots = engine.rootObjects();
    for (auto* root : roots) delete root;
  });
  const auto exit_code = app.exec();
  // A forced proof/programmatic exit can bypass the window close guard. Finish
  // accepted catalog writes while Qt paths, logging and application state live.
  photo_editor::desktop::ExportJobController::finishPendingAdmissions();
  return exit_code;
}
