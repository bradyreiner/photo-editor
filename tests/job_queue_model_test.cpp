#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>

#include "job_queue_model.h"
#include "job_store.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool wait_for_count(
    photo_editor::desktop::JobQueueModel* model,
    int expected_count,
    int timeout_ms = 3000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (model->jobCount() == expected_count) {
      return true;
    }
  }
  return model->jobCount() == expected_count;
}

QString iso_now() {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

photo_editor::desktop::PersistentJobRecord make_job(
    const QString& job_id,
    const QString& job_kind,
    const QString& status) {
  photo_editor::desktop::PersistentJobRecord record;
  record.job_id = job_id;
  record.job_kind = job_kind;
  record.display_label = job_id;
  record.source_path = QStringLiteral("/tmp/%1").arg(job_id);
  record.status = status;
  record.created_at = iso_now();
  record.updated_at = record.created_at;
  return record;
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir temp_dir;
  if (!temp_dir.isValid()) {
    return fail("temporary runtime root should be created");
  }

  qputenv("HOME", temp_dir.path().toUtf8());
  qputenv("XDG_DATA_HOME", temp_dir.filePath(QStringLiteral("data")).toUtf8());
  qputenv("XDG_CACHE_HOME", temp_dir.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", temp_dir.filePath(QStringLiteral("app-data")).toUtf8());

  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("JobQueueModelTest"));

  auto running_export = make_job(
      QStringLiteral("export-running"),
      QStringLiteral("asset_export"),
      QStringLiteral("Running"));
  auto running_import = make_job(
      QStringLiteral("import-running"),
      QStringLiteral("catalog_import"),
      QStringLiteral("Running"));
  auto failed_export = make_job(
      QStringLiteral("export-failed"),
      QStringLiteral("asset_export"),
      QStringLiteral("Failed"));
  failed_export.error_message = QStringLiteral("Initial failure");
  auto completed_timeline = make_job(
      QStringLiteral("timeline-complete"),
      QStringLiteral("timeline_render"),
      QStringLiteral("Completed"));

  if (!photo_editor::desktop::upsertPersistedJobRecord(running_export, nullptr) ||
      !photo_editor::desktop::upsertPersistedJobRecord(running_import, nullptr) ||
      !photo_editor::desktop::upsertPersistedJobRecord(failed_export, nullptr) ||
      !photo_editor::desktop::upsertPersistedJobRecord(completed_timeline, nullptr)) {
    return fail("seed jobs should be persisted");
  }

  photo_editor::desktop::JobQueueModel model;
  if (!wait_for_count(&model, 4)) {
    return fail("job queue model should load the persisted jobs");
  }

  auto export_recovered = false;
  auto import_recovered = false;
  auto failed_retryable = false;
  for (int row = 0; row < model.rowCount(); ++row) {
    const auto index = model.index(row, 0);
    const auto job_id = model.data(index, photo_editor::desktop::JobQueueModel::JobIdRole).toString();
    const auto status = model.data(index, photo_editor::desktop::JobQueueModel::StatusRole).toString();
    const auto metadata =
        model.data(index, photo_editor::desktop::JobQueueModel::MetadataRole).toMap();

    if (job_id == QStringLiteral("export-running")) {
      export_recovered = status == QStringLiteral("Queued") &&
                         metadata.value(QStringLiteral("recoveredAfterRestart")).toBool();
    } else if (job_id == QStringLiteral("import-running")) {
      import_recovered = status == QStringLiteral("Failed") &&
                         metadata.value(QStringLiteral("recoveredAfterRestart")).toBool();
    } else if (job_id == QStringLiteral("export-failed")) {
      failed_retryable = status == QStringLiteral("Failed");
    }
  }

  if (!export_recovered) {
    return fail("running export jobs should be recovered as queued work");
  }
  if (!import_recovered) {
    return fail("running non-export jobs should be marked failed on recovery");
  }
  if (!failed_retryable) {
    return fail("failed export jobs should remain failed until retried");
  }
  if (model.activeJobCount() != 1 || model.failedJobCount() != 2 || model.completedJobCount() != 1) {
    return fail("job counts should reflect recovered queue state");
  }
  if (!model.busy() || !model.hasFailures()) {
    return fail("queue summary flags should expose active work and failures");
  }

  if (!model.retryJob(QStringLiteral("export-failed"))) {
    return fail("failed export jobs should be retryable");
  }

  model.refresh();
  auto retry_count = -1;
  auto queued_after_retry = false;
  for (int row = 0; row < model.rowCount(); ++row) {
    const auto index = model.index(row, 0);
    if (model.data(index, photo_editor::desktop::JobQueueModel::JobIdRole).toString() !=
        QStringLiteral("export-failed")) {
      continue;
    }
    queued_after_retry =
        model.data(index, photo_editor::desktop::JobQueueModel::StatusRole).toString() == QStringLiteral("Queued");
    retry_count = model.data(index, photo_editor::desktop::JobQueueModel::MetadataRole)
                      .toMap()
                      .value(QStringLiteral("retryCount"))
                      .toInt();
    break;
  }

  if (!queued_after_retry || retry_count != 1) {
    return fail("retrying a failed export should requeue it and increment retry metadata");
  }

  auto failed_timeline = make_job(
      QStringLiteral("timeline-failed"),
      QStringLiteral("timeline_render"),
      QStringLiteral("Failed"));
  failed_timeline.error_message = QStringLiteral("Render interrupted");
  auto failed_import = make_job(
      QStringLiteral("import-failed"),
      QStringLiteral("catalog_import"),
      QStringLiteral("Failed"));
  failed_import.error_message = QStringLiteral("Import interrupted");
  if (!photo_editor::desktop::upsertPersistedJobRecord(failed_timeline, nullptr) ||
      !photo_editor::desktop::upsertPersistedJobRecord(failed_import, nullptr)) {
    return fail("retry policy test jobs should be persisted");
  }
  model.refresh();
  if (!model.retryJob(QStringLiteral("timeline-failed"))) {
    return fail("failed timeline render jobs should be retryable");
  }
  if (model.retryJob(QStringLiteral("import-failed"))) {
    return fail("failed import jobs should not be advertised as retryable until import replay exists");
  }
  model.refresh();
  auto timeline_queued_after_retry = false;
  auto import_still_failed_after_retry = false;
  for (int row = 0; row < model.rowCount(); ++row) {
    const auto index = model.index(row, 0);
    const auto job_id = model.data(index, photo_editor::desktop::JobQueueModel::JobIdRole).toString();
    const auto status = model.data(index, photo_editor::desktop::JobQueueModel::StatusRole).toString();
    if (job_id == QStringLiteral("timeline-failed")) {
      timeline_queued_after_retry = status == QStringLiteral("Queued");
    } else if (job_id == QStringLiteral("import-failed")) {
      import_still_failed_after_retry = status == QStringLiteral("Failed");
    }
  }
  if (!timeline_queued_after_retry || !import_still_failed_after_retry) {
    return fail("retry should requeue supported failed jobs and leave unsupported failures unchanged");
  }

  auto queued_timeline = make_job(
      QStringLiteral("timeline-queued"),
      QStringLiteral("timeline_render"),
      QStringLiteral("Queued"));
  auto running_export_two = make_job(
      QStringLiteral("export-running-two"),
      QStringLiteral("asset_export"),
      QStringLiteral("Running"));
  auto queued_import = make_job(
      QStringLiteral("import-queued"),
      QStringLiteral("catalog_import"),
      QStringLiteral("Queued"));
  auto running_import_two = make_job(
      QStringLiteral("import-running-two"),
      QStringLiteral("catalog_import"),
      QStringLiteral("Running"));
  auto queued_model = make_job(
      QStringLiteral("model-queued"),
      QStringLiteral("ai_model_inference"),
      QStringLiteral("Queued"));
  auto running_model = make_job(
      QStringLiteral("model-running"),
      QStringLiteral("ai_model_inference"),
      QStringLiteral("Running"));
  if (!photo_editor::desktop::upsertPersistedJobRecord(queued_timeline, nullptr) ||
      !photo_editor::desktop::upsertPersistedJobRecord(running_export_two, nullptr) ||
      !photo_editor::desktop::upsertPersistedJobRecord(queued_import, nullptr) ||
      !photo_editor::desktop::upsertPersistedJobRecord(running_import_two, nullptr) ||
      !photo_editor::desktop::upsertPersistedJobRecord(queued_model, nullptr) ||
      !photo_editor::desktop::upsertPersistedJobRecord(running_model, nullptr)) {
    return fail("cancel test jobs should be persisted");
  }

  model.refresh();
  if (!model.canCancelJob(QStringLiteral("timeline-queued")) ||
      !model.canCancelJob(QStringLiteral("export-running-two")) ||
      !model.canCancelJob(QStringLiteral("import-queued")) ||
      !model.canCancelJob(QStringLiteral("model-queued")) ||
      model.canCancelJob(QStringLiteral("import-running-two")) ||
      model.canCancelJob(QStringLiteral("model-running")) ||
      model.cancellationPolicySignature(
          QStringLiteral("asset_export"),
          QStringLiteral("Running")) !=
          QStringLiteral("asset_export|Running|cancel:true|mode:cooperative") ||
      model.cancellationPolicySignature(
          QStringLiteral("catalog_import"),
          QStringLiteral("Running")) !=
          QStringLiteral("catalog_import|Running|cancel:false|mode:unsupported") ||
      model.cancellationPolicySignature(
          QStringLiteral("ai_model_inference"),
          QStringLiteral("Queued")) !=
          QStringLiteral("ai_model_inference|Queued|cancel:true|mode:queued-drop")) {
    return fail("job cancellation policy should match queued and cooperative-running support exactly");
  }
  if (!model.cancelJob(QStringLiteral("timeline-queued"))) {
    return fail("queued timeline jobs should be cancelable");
  }
  if (!model.cancelJob(QStringLiteral("export-running-two"))) {
    return fail("running export jobs should accept cooperative cancellation");
  }
  if (!model.cancelJob(QStringLiteral("import-queued"))) {
    return fail("queued import jobs should be cancelable");
  }
  if (!model.cancelJob(QStringLiteral("model-queued"))) {
    return fail("queued model jobs should be cancelable");
  }
  if (model.cancelJob(QStringLiteral("import-running-two")) ||
      model.cancelJob(QStringLiteral("model-running"))) {
    return fail("running import and model jobs should not show unsupported cancellation as available");
  }

  model.refresh();
  auto canceled_timeline_ok = false;
  auto canceling_export_ok = false;
  auto canceled_import_ok = false;
  auto canceled_model_ok = false;
  auto running_import_not_cancelable_ok = false;
  auto running_model_not_cancelable_ok = false;
  for (int row = 0; row < model.rowCount(); ++row) {
    const auto index = model.index(row, 0);
    const auto job_id = model.data(index, photo_editor::desktop::JobQueueModel::JobIdRole).toString();
    const auto status = model.data(index, photo_editor::desktop::JobQueueModel::StatusRole).toString();
    if (job_id == QStringLiteral("timeline-queued")) {
      canceled_timeline_ok = status == QStringLiteral("Canceled");
    } else if (job_id == QStringLiteral("export-running-two")) {
      canceling_export_ok = status == QStringLiteral("Canceling");
    } else if (job_id == QStringLiteral("import-queued")) {
      canceled_import_ok = status == QStringLiteral("Canceled");
    } else if (job_id == QStringLiteral("model-queued")) {
      canceled_model_ok = status == QStringLiteral("Canceled");
    } else if (job_id == QStringLiteral("import-running-two")) {
      running_import_not_cancelable_ok = status == QStringLiteral("Running");
    } else if (job_id == QStringLiteral("model-running")) {
      running_model_not_cancelable_ok = status == QStringLiteral("Running");
    }
  }

  if (!canceled_timeline_ok) {
    return fail("queued jobs should transition directly to canceled");
  }
  if (!canceling_export_ok) {
    return fail("running export jobs should transition to canceling");
  }
  if (!canceled_import_ok || !canceled_model_ok ||
      !running_import_not_cancelable_ok || !running_model_not_cancelable_ok) {
    return fail("queued import/model jobs should cancel while unsupported running work stays running");
  }
  model.removeJob(QStringLiteral("import-running-two"));
  model.removeJob(QStringLiteral("model-running"));

  model.clearFinishedJobs();
  model.refresh();
  if (model.jobCount() != 4 || model.failedJobCount() != 0 || model.completedJobCount() != 0) {
    return fail("clearing finished jobs should leave only active queued/canceling work behind");
  }

  qunsetenv("PHOTO_EDITOR_APP_DATA_ROOT");
  qunsetenv("XDG_CACHE_HOME");
  qunsetenv("XDG_DATA_HOME");
  qunsetenv("HOME");
  return EXIT_SUCCESS;
}
