#include <algorithm>
#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
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
    if (!model->loading() && model->jobCount() == expected_count) {
      return true;
    }
  }
  return !model->loading() && model->jobCount() == expected_count;
}

bool refresh_and_wait(photo_editor::desktop::JobQueueModel& model) {
  model.refresh();
  QElapsedTimer timer;
  timer.start();
  while (model.loading() && timer.elapsed() < 10000)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return !model.loading();
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
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", temp_dir.filePath(QStringLiteral("app-data")).toUtf8());
  qunsetenv("PHOTO_EDITOR_DATABASE_URL");

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
  model.initializeFromStore();
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
      export_recovered = status == QStringLiteral("Paused") &&
                         metadata.value(QStringLiteral("recoveredAfterRestart")).toBool() &&
                         metadata.value(QStringLiteral("requiresRecoveryConfirmation")).toBool() &&
                         metadata.value(QStringLiteral("recoveryAction")).toString() ==
                             QStringLiteral("awaiting-confirmation");
    } else if (job_id == QStringLiteral("import-running")) {
      import_recovered = status == QStringLiteral("Failed") &&
                         metadata.value(QStringLiteral("recoveredAfterRestart")).toBool();
    } else if (job_id == QStringLiteral("export-failed")) {
      failed_retryable = status == QStringLiteral("Failed");
    }
  }

  if (!export_recovered) {
    for (int row = 0; row < model.rowCount(); ++row) {
      const auto index = model.index(row, 0);
      std::cerr << "observed job="
                << model.data(index, photo_editor::desktop::JobQueueModel::JobIdRole).toString().toStdString()
                << " status="
                << model.data(index, photo_editor::desktop::JobQueueModel::StatusRole).toString().toStdString()
                << " metadata="
                << QJsonDocument::fromVariant(
                       model.data(index, photo_editor::desktop::JobQueueModel::MetadataRole))
                       .toJson(QJsonDocument::Compact)
                       .toStdString()
                << '\n';
    }
    return fail("running export jobs should wait for explicit recovery confirmation");
  }
  if (!import_recovered) {
    return fail("running non-export jobs should be marked failed on recovery");
  }
  if (!failed_retryable) {
    return fail("failed export jobs should remain failed until retried");
  }
  if (model.activeJobCount() != 0 || model.failedJobCount() != 2 || model.completedJobCount() != 1) {
    return fail("job counts should reflect recovered queue state");
  }
  if (model.busy() || !model.hasFailures() || !model.hasRecoveryReport() ||
      model.recoveredJobCount() != 2 ||
      !model.recoverySummary().contains(QStringLiteral("1 waiting for review")) ||
      model.recoveryDetails().size() != 2) {
    return fail("queue summary flags should expose incomplete recovered work without replaying it");
  }
  if (!model.canResumeJob(QStringLiteral("export-running")) ||
      !model.resumeJob(QStringLiteral("export-running"))) {
    return fail("recovered exports should resume only after explicit confirmation");
  }
  const auto export_progress = model.progressSummaryForKind(QStringLiteral("asset_export"));
  if (export_progress.value(QStringLiteral("total")).toInt() != 2 ||
      export_progress.value(QStringLiteral("active")).toInt() != 1 ||
      export_progress.value(QStringLiteral("failed")).toInt() != 1) {
    return fail("export progress summary should report overall active and failed delivery counts");
  }

  if (!model.retryJob(QStringLiteral("export-failed"))) {
    return fail("failed export jobs should be retryable");
  }

  if (!refresh_and_wait(model)) return fail("job history refresh should settle");
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

  model.acknowledgeRecoveryReport();
  if (model.hasRecoveryReport() || model.recoveredJobCount() != 0) {
    return fail("acknowledging recovery should dismiss the report without deleting job history");
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
  if (!refresh_and_wait(model)) return fail("job history refresh should settle");
  if (!model.retryJob(QStringLiteral("timeline-failed"))) {
    return fail("failed timeline render jobs should be retryable");
  }
  if (!model.retryJob(QStringLiteral("import-failed"))) {
    return fail("failed import jobs should retry the same persisted source, mode, selection, and preflight options");
  }
  if (!refresh_and_wait(model)) return fail("job history refresh should settle");
  auto timeline_queued_after_retry = false;
  auto import_queued_after_retry = false;
  for (int row = 0; row < model.rowCount(); ++row) {
    const auto index = model.index(row, 0);
    const auto job_id = model.data(index, photo_editor::desktop::JobQueueModel::JobIdRole).toString();
    const auto status = model.data(index, photo_editor::desktop::JobQueueModel::StatusRole).toString();
    if (job_id == QStringLiteral("timeline-failed")) {
      timeline_queued_after_retry = status == QStringLiteral("Queued");
    } else if (job_id == QStringLiteral("import-failed")) {
      import_queued_after_retry = status == QStringLiteral("Queued");
    }
  }
  if (!timeline_queued_after_retry || !import_queued_after_retry) {
    return fail("retry should requeue failed timeline and import jobs");
  }
  if (!model.setJobPriority(QStringLiteral("timeline-failed"), QStringLiteral("high"))) {
    return fail("queued background jobs should accept a persisted priority change");
  }
  const auto center_jobs = model.jobsForCenter();
  auto enriched_timeline_job = false;
  for (const auto& value : center_jobs) {
    const auto job = value.toMap();
    if (job.value(QStringLiteral("jobId")).toString() !=
        QStringLiteral("timeline-failed")) {
      continue;
    }
    enriched_timeline_job =
        job.value(QStringLiteral("stage")).toString() ==
            QStringLiteral("Waiting in queue") &&
        job.value(QStringLiteral("priority")).toString() ==
            QStringLiteral("high") &&
        job.value(QStringLiteral("canChangePriority")).toBool() &&
        job.value(QStringLiteral("canCancel")).toBool() &&
        job.value(QStringLiteral("canPause")).toBool() &&
        job.value(QStringLiteral("affectedType")).toString() ==
            QStringLiteral("timeline") &&
        job.value(QStringLiteral("jumpLabel")).toString() ==
            QStringLiteral("Open timeline");
  }
  if (!enriched_timeline_job ||
      model.affectedItemForJob(QStringLiteral("timeline-failed"))
              .value(QStringLiteral("type")).toString() !=
          QStringLiteral("timeline") ||
      model.setJobPriority(QStringLiteral("timeline-failed"), QStringLiteral("urgent")) ||
      model.setJobPriority(QStringLiteral("timeline-complete"), QStringLiteral("low"))) {
    return fail("the job-center model should expose stage, priority, controls, and affected-item routing without accepting invalid or active-job reprioritization");
  }

  auto completed_import_after_failure = make_job(
      QStringLiteral("import-completed-after-failure"),
      QStringLiteral("catalog_import"),
      QStringLiteral("Completed"));
  completed_import_after_failure.created_at = QStringLiteral("2999-01-01T00:00:00Z");
  completed_import_after_failure.updated_at = completed_import_after_failure.created_at;
  if (!photo_editor::desktop::upsertPersistedJobRecord(completed_import_after_failure, nullptr)) {
    return fail("a successful import after a failed attempt should be persisted");
  }
  if (!refresh_and_wait(model)) return fail("job history refresh should settle");
  if (model.failedJobCountForKind(QStringLiteral("catalog_import")) < 1 ||
      model.hasCurrentFailureForKind(QStringLiteral("catalog_import"))) {
    return fail("historical import failures should remain inspectable without stale global failure chrome");
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

  if (!refresh_and_wait(model)) return fail("job history refresh should settle");
  if (!model.canCancelJob(QStringLiteral("timeline-queued")) ||
      !model.canCancelJob(QStringLiteral("export-running-two")) ||
      !model.canCancelJob(QStringLiteral("import-queued")) ||
      !model.canCancelJob(QStringLiteral("import-running-two")) ||
      !model.canPauseJob(QStringLiteral("import-queued")) ||
      !model.canPauseJob(QStringLiteral("import-running-two")) ||
      !model.canCancelJob(QStringLiteral("model-queued")) ||
      model.canCancelJob(QStringLiteral("model-running")) ||
      model.cancellationPolicySignature(
          QStringLiteral("asset_export"),
          QStringLiteral("Running")) !=
          QStringLiteral("asset_export|Running|cancel:true|mode:cooperative") ||
      model.cancellationPolicySignature(
          QStringLiteral("catalog_import"),
          QStringLiteral("Running")) !=
          QStringLiteral("catalog_import|Running|cancel:true|mode:cooperative") ||
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
  if (!model.pauseJob(QStringLiteral("import-queued"))) {
    return fail("queued imports should pause before processing starts");
  }
  if (!refresh_and_wait(model)) return fail("job history refresh should settle");
  if (!model.canResumeJob(QStringLiteral("import-queued")) ||
      !model.resumeJob(QStringLiteral("import-queued"))) {
    return fail("paused imports should resume with their persisted options");
  }
  if (!model.cancelJob(QStringLiteral("import-queued"))) {
    return fail("queued import jobs should be cancelable");
  }
  if (!model.cancelJob(QStringLiteral("import-running-two"))) {
    return fail("running import jobs should accept cooperative cancellation");
  }
  if (!model.cancelJob(QStringLiteral("model-queued"))) {
    return fail("queued model jobs should be cancelable");
  }
  if (model.cancelJob(QStringLiteral("model-running"))) {
    return fail("running model jobs should not show unsupported cancellation as available");
  }

  if (!refresh_and_wait(model)) return fail("job history refresh should settle");
  auto canceled_timeline_ok = false;
  auto canceling_export_ok = false;
  auto canceled_import_ok = false;
  auto canceled_model_ok = false;
  auto canceling_import_ok = false;
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
      canceling_import_ok = status == QStringLiteral("Canceling");
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
      !canceling_import_ok || !running_model_not_cancelable_ok) {
    return fail("queued jobs should cancel, running imports should stop cooperatively, and unsupported running work should stay running");
  }
  model.removeJob(QStringLiteral("import-running-two"));
  model.removeJob(QStringLiteral("model-running"));

  model.clearFinishedJobs();
  if (!refresh_and_wait(model)) return fail("job history refresh should settle");
  if (model.jobCount() != 5 || model.failedJobCount() != 0 || model.completedJobCount() != 0) {
    return fail("clearing finished jobs should leave only active queued/canceling work behind");
  }

  // Exercise the persistent render queue in isolation so unrelated import and
  // model jobs cannot affect manual-order adjacency.
  for (const auto& record : photo_editor::desktop::loadPersistedJobRecords()) {
    if (!photo_editor::desktop::removePersistedJobRecord(record.job_id, nullptr)) {
      return fail("render queue fixture should clear prior job history");
    }
  }

  auto render_one = make_job(
      QStringLiteral("render-one"), QStringLiteral("asset_export"), QStringLiteral("Queued"));
  render_one.source_path = temp_dir.filePath(QStringLiteral("source-one.png"));
  render_one.destination_path = temp_dir.path();
  render_one.preset_name = QStringLiteral("Web Proof");
  render_one.recipe_json = QStringLiteral("{\"exposure\":0.5}");
  render_one.metadata_json.insert(QStringLiteral("priority"), QStringLiteral("normal"));
  render_one.metadata_json.insert(QStringLiteral("queueOrder"), 1);

  auto render_two = make_job(
      QStringLiteral("render-two"), QStringLiteral("timeline_render"), QStringLiteral("Queued"));
  render_two.source_path = temp_dir.filePath(QStringLiteral("timeline-two.json"));
  render_two.output_path = temp_dir.filePath(QStringLiteral("timeline-two.mp4"));
  render_two.preset_name = QStringLiteral("Social 1080p");
  render_two.recipe_json = QStringLiteral("{\"timeline\":true}");
  render_two.metadata_json.insert(QStringLiteral("priority"), QStringLiteral("normal"));
  render_two.metadata_json.insert(QStringLiteral("queueOrder"), 2);
  render_two.metadata_json.insert(QStringLiteral("timelineId"), QStringLiteral("timeline-two"));
  render_two.metadata_json.insert(QStringLiteral("customDeliveryOption"), QStringLiteral("kept"));
  render_two.metadata_json.insert(QStringLiteral("artifactVerification"), QJsonObject{{QStringLiteral("valid"), true}});

  auto render_three = make_job(
      QStringLiteral("render-three"), QStringLiteral("asset_export"), QStringLiteral("Queued"));
  render_three.source_path = temp_dir.filePath(QStringLiteral("source-three.png"));
  render_three.destination_path = temp_dir.path();
  render_three.metadata_json.insert(QStringLiteral("priority"), QStringLiteral("normal"));
  render_three.metadata_json.insert(QStringLiteral("queueOrder"), 3);

  auto high_render = make_job(
      QStringLiteral("render-high"), QStringLiteral("timeline_render"), QStringLiteral("Queued"));
  high_render.output_path = temp_dir.filePath(QStringLiteral("timeline-high.mp4"));
  high_render.metadata_json.insert(QStringLiteral("priority"), QStringLiteral("high"));
  high_render.metadata_json.insert(QStringLiteral("queueOrder"), 4);

  auto canceled_render = make_job(
      QStringLiteral("render-canceled"), QStringLiteral("timeline_render"), QStringLiteral("Canceled"));
  canceled_render.output_path = temp_dir.filePath(QStringLiteral("timeline-canceled.mp4"));

  auto completed_render = make_job(
      QStringLiteral("render-completed"), QStringLiteral("timeline_render"), QStringLiteral("Completed"));
  completed_render.output_path = temp_dir.filePath(QStringLiteral("timeline-completed.mp4"));
  QFile completed_output(completed_render.output_path);
  if (!completed_output.open(QIODevice::WriteOnly) ||
      completed_output.write("render-output") <= 0) {
    return fail("completed render fixture output should be writable");
  }
  completed_output.close();

  for (const auto& record : {render_one, render_two, render_three, high_render,
                             canceled_render, completed_render}) {
    if (!photo_editor::desktop::upsertPersistedJobRecord(record, nullptr)) {
      return fail("persistent render queue fixtures should be stored");
    }
  }
  if (!refresh_and_wait(model)) return fail("job history refresh should settle");

  if (!model.canMoveRenderJob(QStringLiteral("render-two"), -1) ||
      !model.canMoveRenderJob(QStringLiteral("render-two"), 1) ||
      model.canMoveRenderJob(QStringLiteral("render-one"), -1) ||
      model.canMoveRenderJob(QStringLiteral("render-high"), -1) ||
      model.moveRenderJob(QStringLiteral("render-high"), -1) ||
      !model.moveRenderJob(QStringLiteral("render-two"), -1)) {
    return fail("manual render order should move only within the same persistent priority group");
  }

  photo_editor::desktop::JobQueueModel relaunched_model;
  if (!refresh_and_wait(relaunched_model)) return fail("relaunch history should settle");
  QStringList queued_order;
  for (int row = 0; row < relaunched_model.rowCount(); ++row) {
    const auto index = relaunched_model.index(row, 0);
    const auto status = relaunched_model.data(
        index, photo_editor::desktop::JobQueueModel::StatusRole).toString();
    if (status == QStringLiteral("Queued") || status == QStringLiteral("Paused")) {
      queued_order.push_back(relaunched_model.data(
          index, photo_editor::desktop::JobQueueModel::JobIdRole).toString());
    }
  }
  if (queued_order.indexOf(QStringLiteral("render-high")) != 0 ||
      queued_order.indexOf(QStringLiteral("render-two")) < 0 ||
      queued_order.indexOf(QStringLiteral("render-one")) < 0 ||
      queued_order.indexOf(QStringLiteral("render-two")) >
          queued_order.indexOf(QStringLiteral("render-one"))) {
    return fail("priority then manual render order should survive a fresh model instance");
  }

  if (!model.setJobPriority(QStringLiteral("render-three"), QStringLiteral("high"))) {
    return fail("queued render priority should persist independently from manual order");
  }
  if (!model.setRenderResourcePolicy(
          QStringLiteral("render-two"), QStringLiteral("balanced")) ||
      model.setRenderResourcePolicy(
          QStringLiteral("render-two"), QStringLiteral("maximum")) ||
      model.setRenderResourcePolicy(
          QStringLiteral("render-completed"), QStringLiteral("export-first"))) {
    return fail("only queued or paused renders should accept one of the three explicit resource policies");
  }
  const auto duplicate_id = model.duplicateRenderJob(QStringLiteral("render-two"));
  if (duplicate_id.isEmpty() || duplicate_id == QStringLiteral("render-two")) {
    return fail("inactive timeline renders should duplicate into a new queued job");
  }
  const auto records_after_duplicate = photo_editor::desktop::loadPersistedJobRecords();
  const auto duplicate_it = std::find_if(
      records_after_duplicate.cbegin(), records_after_duplicate.cend(),
      [&](const auto& record) { return record.job_id == duplicate_id; });
  if (duplicate_it == records_after_duplicate.cend() ||
      duplicate_it->status != QStringLiteral("Queued") ||
      duplicate_it->source_path != render_two.source_path ||
      duplicate_it->recipe_json != render_two.recipe_json ||
      duplicate_it->preset_name != render_two.preset_name ||
      duplicate_it->output_path == render_two.output_path ||
      !duplicate_it->output_path.contains(QStringLiteral("-copy")) ||
      duplicate_it->metadata_json.value(QStringLiteral("customDeliveryOption")).toString() !=
          QStringLiteral("kept") ||
      duplicate_it->metadata_json.value(QStringLiteral("resourcePolicy")).toString() !=
          QStringLiteral("balanced") ||
      duplicate_it->metadata_json.value(QStringLiteral("resourcePolicyAffectsQuality")).toBool() ||
      duplicate_it->metadata_json.contains(QStringLiteral("artifactVerification")) ||
      duplicate_it->metadata_json.value(QStringLiteral("duplicatedFromJobId")).toString() !=
          QStringLiteral("render-two")) {
    return fail("duplicate should preserve delivery intent while clearing runtime results and allocating a collision-safe output");
  }

  if (model.revealOutputPath(QStringLiteral("render-completed")) !=
          QFileInfo(completed_render.output_path).absoluteFilePath() ||
      !model.revealOutputPath(QStringLiteral("render-two")).isEmpty()) {
    return fail("reveal output should resolve only readable completed render artifacts");
  }
  if (!model.pauseJob(QStringLiteral("render-two")) ||
      !model.canResumeJob(QStringLiteral("render-two")) ||
      !model.resumeJob(QStringLiteral("render-two"))) {
    return fail("queued timeline renders should pause and resume without losing their persisted plan");
  }
  if (!model.retryJob(QStringLiteral("render-canceled"))) {
    return fail("canceled timeline renders should retry from the same persisted delivery plan");
  }

  photo_editor::desktop::JobQueueModel final_relaunch_model;
  if (!refresh_and_wait(final_relaunch_model)) return fail("final relaunch history should settle");
  if (final_relaunch_model.totalJobCountForKind(QStringLiteral("timeline_render")) != 5 ||
      final_relaunch_model.revealOutputPath(QStringLiteral("render-completed")).isEmpty()) {
    return fail("render queue jobs, duplicates, controls, and completed artifacts should survive relaunch");
  }

  qunsetenv("PHOTO_EDITOR_APP_DATA_ROOT");
  qunsetenv("XDG_CACHE_HOME");
  qunsetenv("XDG_DATA_HOME");
  qunsetenv("HOME");
  return EXIT_SUCCESS;
}
