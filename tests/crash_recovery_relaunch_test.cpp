#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#include "app_shell_controller.h"
#include "edit_adjustment_patch.h"
#include "job_queue_model.h"
#include "job_store.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool wait_for_history(photo_editor::desktop::JobQueueModel& model) {
  QElapsedTimer timer;
  timer.start();
  while (model.loading() && timer.elapsed() < 10000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    QThread::msleep(1);
  }
  return !model.loading();
}

bool write_ready_marker(const QString& root) {
  QFile marker(QDir(root).filePath(QStringLiteral("worker-ready")));
  if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  return marker.write("ready") == 5;
}

photo_editor::desktop::PersistentJobRecord job(
    const QString& id,
    const QString& kind,
    const QString& status) {
  photo_editor::desktop::PersistentJobRecord record;
  record.job_id = id;
  record.job_kind = kind;
  record.display_label = id;
  record.status = status;
  record.created_at = QStringLiteral("2026-07-14T12:00:00Z");
  record.updated_at = record.created_at;
  return record;
}

int run_worker(QCoreApplication* app, const QString& mode, const QString& root) {
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", root.toUtf8());
  if (mode == QStringLiteral("edit")) {
    photo_editor::desktop::AppShellController shell;
    shell.setCurrentWorkspace(1);
    photo_editor::desktop::EditRecipeState state;
    state.exposure = 1.25;
    state.contrast = 12.0;
    shell.updateSessionState(
        QStringLiteral("force-quit-edit-asset"),
        {},
        QStringLiteral("Recovery Project"),
        photo_editor::desktop::serialize_adjustment_patch_json(
            state,
            photo_editor::desktop::all_adjustment_groups(),
            photo_editor::desktop::
                AdjustmentPatchScope::AssetBound),
        QStringLiteral("masks"));
  } else if (mode == QStringLiteral("import")) {
    auto interrupted = job(
        QStringLiteral("force-quit-import"),
        QStringLiteral("catalog_import"),
        QStringLiteral("Running"));
    interrupted.metadata_json.insert(QStringLiteral("importPlanSignature"), QStringLiteral("reviewed:v1"));
    auto safe_queued = job(
        QStringLiteral("safe-queued-work"),
        QStringLiteral("catalog_import"),
        QStringLiteral("Queued"));
    if (!photo_editor::desktop::upsertPersistedJobRecord(interrupted, nullptr) ||
        !photo_editor::desktop::upsertPersistedJobRecord(safe_queued, nullptr)) {
      return fail("worker should persist import recovery fixtures");
    }
  } else if (mode == QStringLiteral("export")) {
    const auto output_path = QDir(root).filePath(QStringLiteral("already-delivered.jpg"));
    QFile output(output_path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        output.write("delivered-once") != 14) {
      return fail("worker should persist the already delivered export artifact");
    }
    output.close();
    auto interrupted = job(
        QStringLiteral("force-quit-export"),
        QStringLiteral("asset_export"),
        QStringLiteral("Running"));
    interrupted.output_path = output_path;
    interrupted.destination_path = root;
    interrupted.metadata_json.insert(QStringLiteral("deliveryPlanSignature"), QStringLiteral("reviewed:v1"));
    if (!photo_editor::desktop::upsertPersistedJobRecord(interrupted, nullptr)) {
      return fail("worker should persist export recovery fixture");
    }
  } else {
    return fail("worker mode should be recognized");
  }

  if (!write_ready_marker(root)) {
    return fail("worker should publish its ready marker");
  }
  return app->exec();
}

bool force_terminate_worker(const QString& executable, const QString& mode, const QString& root) {
  QProcess process;
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("PHOTO_EDITOR_APP_DATA_ROOT"), root);
  process.setProcessEnvironment(environment);
  process.start(executable, {QStringLiteral("--worker"), mode, root});
  if (!process.waitForStarted(3000)) {
    return false;
  }
  const auto marker_path = QDir(root).filePath(QStringLiteral("worker-ready"));
  QElapsedTimer timer;
  timer.start();
  while (!QFile::exists(marker_path) && timer.elapsed() < 5000) {
    QThread::msleep(10);
  }
  if (!QFile::exists(marker_path)) {
    process.kill();
    process.waitForFinished(3000);
    return false;
  }
  process.kill();
  return process.waitForFinished(3000) && process.exitStatus() == QProcess::CrashExit;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("CrashRecoveryRelaunchTest"));

  const auto arguments = app.arguments();
  if (arguments.size() == 4 && arguments.at(1) == QStringLiteral("--worker")) {
    return run_worker(&app, arguments.at(2), arguments.at(3));
  }

  QTemporaryDir runtime;
  if (!runtime.isValid()) {
    return fail("temporary recovery runtime should be created");
  }

  const auto edit_root = runtime.filePath(QStringLiteral("edit"));
  QDir().mkpath(edit_root);
  if (!force_terminate_worker(arguments.first(), QStringLiteral("edit"), edit_root)) {
    return fail("edit worker should be force-terminated after durable state is written");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", edit_root.toUtf8());
  photo_editor::desktop::AppShellController recovered_shell;
  if (!recovered_shell.sessionRecoveryAvailable() ||
      recovered_shell.recoveredWorkspace() != 1 ||
      recovered_shell.recoveredAssetId() != QStringLiteral("force-quit-edit-asset") ||
      recovered_shell.recoveredProject() != QStringLiteral("Recovery Project") ||
      recovered_shell.recoveredAdjustmentPatchJson().isEmpty()) {
    return fail("edit relaunch should restore the exact committed session adjustments");
  }
  recovered_shell.markCleanExit();

  const auto import_root = runtime.filePath(QStringLiteral("import"));
  QDir().mkpath(import_root);
  if (!force_terminate_worker(arguments.first(), QStringLiteral("import"), import_root)) {
    return fail("import worker should be force-terminated after its reviewed plan is durable");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", import_root.toUtf8());
  photo_editor::desktop::JobQueueModel import_queue;
  import_queue.initializeFromStore();
  if (!wait_for_history(import_queue)) return fail("import recovery history should settle");
  auto interrupted_import_failed = false;
  auto safe_work_preserved = false;
  for (int row = 0; row < import_queue.rowCount(); ++row) {
    const auto index = import_queue.index(row, 0);
    const auto id = import_queue.data(index, photo_editor::desktop::JobQueueModel::JobIdRole).toString();
    const auto status = import_queue.data(index, photo_editor::desktop::JobQueueModel::StatusRole).toString();
    if (id == QStringLiteral("force-quit-import")) {
      interrupted_import_failed = status == QStringLiteral("Failed");
    } else if (id == QStringLiteral("safe-queued-work")) {
      safe_work_preserved = status == QStringLiteral("Queued");
    }
  }
  if (!interrupted_import_failed || !safe_work_preserved ||
      !import_queue.hasRecoveryReport()) {
    return fail("import relaunch should report incomplete work and preserve only known-safe queued work");
  }

  const auto export_root = runtime.filePath(QStringLiteral("export"));
  QDir().mkpath(export_root);
  if (!force_terminate_worker(arguments.first(), QStringLiteral("export"), export_root)) {
    return fail("export worker should be force-terminated after its delivery state is durable");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", export_root.toUtf8());
  photo_editor::desktop::JobQueueModel export_queue;
  export_queue.initializeFromStore();
  if (!wait_for_history(export_queue)) return fail("export recovery history should settle");
  const auto output_path = QDir(export_root).filePath(QStringLiteral("already-delivered.jpg"));
  QFile delivered(output_path);
  if (!delivered.open(QIODevice::ReadOnly) || delivered.readAll() != QByteArray("delivered-once") ||
      QFile::exists(QDir(export_root).filePath(QStringLiteral("already-delivered (1).jpg")))) {
    return fail("export relaunch must not duplicate or overwrite an externally visible delivery");
  }
  auto export_waiting = false;
  for (int row = 0; row < export_queue.rowCount(); ++row) {
    const auto index = export_queue.index(row, 0);
    if (export_queue.data(index, photo_editor::desktop::JobQueueModel::JobIdRole).toString() !=
        QStringLiteral("force-quit-export")) {
      continue;
    }
    const auto metadata =
        export_queue.data(index, photo_editor::desktop::JobQueueModel::MetadataRole).toMap();
    export_waiting =
        export_queue.data(index, photo_editor::desktop::JobQueueModel::StatusRole).toString() ==
            QStringLiteral("Paused") &&
        metadata.value(QStringLiteral("requiresRecoveryConfirmation")).toBool();
  }
  if (!export_waiting || export_queue.busy() ||
      !export_queue.resumeJob(QStringLiteral("force-quit-export"))) {
    return fail("export relaunch should wait for explicit review before requeueing the delivery");
  }

  return EXIT_SUCCESS;
}
