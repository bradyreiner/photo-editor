#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>

#include "edit_recipe_state.h"
#include "export_job_controller.h"
#include "job_runner.h"
#include "job_store.h"
#include "photo_render_utils.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool wait_for_job(
    photo_editor::desktop::ExportJobController* controller,
    int index,
    int timeout_ms = 3000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const auto jobs = controller->jobs();
    if (index >= 0 && index < jobs.size()) {
      const auto status = jobs.at(index).toMap().value(QStringLiteral("status")).toString();
      if (status == QStringLiteral("Completed") || status == QStringLiteral("Failed")) {
        return true;
      }
    }
  }
  return false;
}

bool wait_for_job_id(
    photo_editor::desktop::ExportJobController* controller,
    const QString& job_id,
    int timeout_ms = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const auto jobs = controller->jobs();
    for (const auto& value : jobs) {
      const auto job = value.toMap();
      if (job.value(QStringLiteral("jobId")).toString() != job_id) {
        continue;
      }
      const auto status = job.value(QStringLiteral("status")).toString();
      if (status == QStringLiteral("Completed") || status == QStringLiteral("Failed")) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir temp_dir;
  if (!temp_dir.isValid()) {
    return fail("temporary export test directory should be created");
  }

  qputenv("HOME", temp_dir.path().toUtf8());
  qputenv("XDG_DATA_HOME", temp_dir.filePath(QStringLiteral("data")).toUtf8());
  qputenv("XDG_CACHE_HOME", temp_dir.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", temp_dir.filePath(QStringLiteral("app-data")).toUtf8());

  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("ExportJobControllerTest"));

  const auto source_path = temp_dir.filePath(QStringLiteral("source.png"));
  const auto export_dir = temp_dir.filePath(QStringLiteral("exports"));
  QDir().mkpath(export_dir);

  QImage source(80, 40, QImage::Format_ARGB32);
  source.fill(Qt::black);
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      source.setPixelColor(x, y, x < 40 ? QColor(240, 32, 32) : QColor(24, 48, 240));
    }
  }
  if (!source.save(source_path)) {
    return fail("source image should be written for export testing");
  }

  photo_editor::desktop::EditRecipeState crop_state;
  crop_state.crop_rect_x = 0.5;
  crop_state.crop_rect_y = 0.0;
  crop_state.crop_rect_width = 0.5;
  crop_state.crop_rect_height = 1.0;
  const auto recipe_json = photo_editor::desktop::serialize_edit_recipe_json(crop_state);

  photo_editor::desktop::JobRunner runner;
  photo_editor::desktop::ExportJobController controller;
  const auto crop_job_index = controller.jobCount();
  if (!controller.queueExport(
          QStringLiteral("asset-crop"),
          QStringLiteral("photo"),
          QStringLiteral("Crop Check"),
          source_path,
          QString(),
          recipe_json,
          QStringLiteral("Full Resolution"),
          export_dir)) {
    return fail("photo export should queue successfully");
  }

  if (!wait_for_job(&controller, crop_job_index)) {
    return fail("rendered photo export should finish");
  }

  const auto rendered_job = controller.jobs().at(crop_job_index).toMap();
  if (rendered_job.value(QStringLiteral("status")).toString() != QStringLiteral("Completed")) {
    return fail("rendered photo export should complete successfully");
  }

  const auto rendered_path = rendered_job.value(QStringLiteral("outputPath")).toString();
  const QFileInfo rendered_info(rendered_path);
  if (rendered_info.suffix().toLower() != QStringLiteral("jpg")) {
    return fail("rendered photo export should write a jpeg");
  }

  QImage rendered_image(rendered_path);
  if (rendered_image.isNull()) {
    return fail("rendered photo export should produce a readable image");
  }
  if (rendered_image.width() != 40 || rendered_image.height() != 40) {
    return fail("rendered photo export should honor the crop dimensions");
  }

  const auto sample = rendered_image.pixelColor(rendered_image.width() / 2, rendered_image.height() / 2);
  if (sample.blue() < 160 || sample.red() > 120) {
    return fail("rendered photo export should contain the cropped right-half content");
  }

  const auto copy_job_index = controller.jobCount();
  if (!controller.queueExport(
          QStringLiteral("asset-copy"),
          QStringLiteral("photo"),
          QStringLiteral("Original Copy"),
          source_path,
          QString(),
          QString(),
          QStringLiteral("Original Copy"),
          export_dir)) {
    return fail("original copy export should queue successfully");
  }

  if (!wait_for_job(&controller, copy_job_index)) {
    return fail("original copy export should finish");
  }

  const auto copied_job = controller.jobs().at(copy_job_index).toMap();
  if (copied_job.value(QStringLiteral("status")).toString() != QStringLiteral("Completed")) {
    return fail("original copy export should complete successfully");
  }

  const auto copied_path = copied_job.value(QStringLiteral("outputPath")).toString();
  const QFileInfo copied_info(copied_path);
  if (copied_info.suffix().toLower() != QStringLiteral("png")) {
    return fail("original copy export should preserve the source file extension");
  }

  QFile source_file(source_path);
  QFile copied_file(copied_path);
  if (!source_file.open(QIODevice::ReadOnly) || !copied_file.open(QIODevice::ReadOnly)) {
    return fail("exported original copy should be readable");
  }
  if (source_file.readAll() != copied_file.readAll()) {
    return fail("original copy export should preserve the original file bytes");
  }

  controller.clearFinishedJobs();
  if (controller.jobCount() != 0) {
    return fail("clearFinishedJobs should empty the queue before recovery testing");
  }

  photo_editor::desktop::PersistentJobRecord recovered_record;
  recovered_record.job_id = QStringLiteral("recovered-export");
  recovered_record.job_kind = QStringLiteral("asset_export");
  recovered_record.asset_id = QStringLiteral("asset-recovered");
  recovered_record.media_type = QStringLiteral("photo");
  recovered_record.display_label = QStringLiteral("Recovered Export");
  recovered_record.source_path = source_path;
  recovered_record.preset_name = QStringLiteral("Original Copy");
  recovered_record.destination_path = export_dir;
  recovered_record.status = QStringLiteral("Running");
  recovered_record.created_at = QStringLiteral("2026-03-31T00:00:00Z");
  recovered_record.updated_at = recovered_record.created_at;
  if (!photo_editor::desktop::upsertPersistedJobRecord(recovered_record, nullptr)) {
    return fail("recovered export job should be persisted");
  }

  photo_editor::desktop::JobRunner recovered_runner;
  photo_editor::desktop::ExportJobController recovered_controller;
  if (recovered_controller.jobCount() < 1) {
    return fail("recovered export jobs should load into the queue");
  }
  if (!wait_for_job_id(&recovered_controller, QStringLiteral("recovered-export"), 8000)) {
    return fail("recovered queued export should resume automatically");
  }

  QVariantMap recovered_job;
  for (const auto& value : recovered_controller.jobs()) {
    const auto job = value.toMap();
    if (job.value(QStringLiteral("jobId")).toString() == QStringLiteral("recovered-export")) {
      recovered_job = job;
      break;
    }
  }
  if (recovered_job.isEmpty()) {
    return fail("recovered export should remain visible in the controller");
  }
  if (recovered_job.value(QStringLiteral("status")).toString() != QStringLiteral("Completed")) {
    return fail("recovered export should complete successfully");
  }
  if (!QFileInfo::exists(recovered_job.value(QStringLiteral("outputPath")).toString())) {
    return fail("recovered export should write its output file");
  }

  const auto subject_source_path = temp_dir.filePath(QStringLiteral("subject-source.png"));
  QImage subject_source(64, 32, QImage::Format_ARGB32);
  subject_source.fill(QColor(96, 96, 96));
  if (!subject_source.save(subject_source_path)) {
    return fail("subject source image should be written for export testing");
  }

  photo_editor::desktop::EditRecipeState subject_state;
  QVariantMap subject_mask;
  subject_mask.insert(QStringLiteral("id"), QStringLiteral("subject-export"));
  subject_mask.insert(QStringLiteral("name"), QStringLiteral("AI Subject"));
  subject_mask.insert(QStringLiteral("kind"), QStringLiteral("subject"));
  subject_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  subject_mask.insert(QStringLiteral("enabled"), true);
  subject_mask.insert(QStringLiteral("exposure"), 1.0);
  subject_mask.insert(QStringLiteral("temperature"), 0.0);
  subject_mask.insert(QStringLiteral("feather"), 0.05);
  subject_mask.insert(QStringLiteral("segmentationData"), QByteArray::fromHex("ff00"));
  subject_mask.insert(QStringLiteral("segmentationWidth"), 2);
  subject_mask.insert(QStringLiteral("segmentationHeight"), 1);
  subject_state.mask_stack = {subject_mask};
  const auto subject_recipe_json = photo_editor::desktop::serialize_edit_recipe_json(subject_state);

  const auto subject_job_index = controller.jobCount();
  if (!controller.queueExport(
          QStringLiteral("asset-subject"),
          QStringLiteral("photo"),
          QStringLiteral("Subject Export"),
          subject_source_path,
          QString(),
          subject_recipe_json,
          QStringLiteral("Full Resolution"),
          export_dir)) {
    return fail("subject mask export should queue successfully");
  }

  if (!wait_for_job(&controller, subject_job_index)) {
    return fail("subject mask export should finish");
  }

  const auto subject_job = controller.jobs().at(subject_job_index).toMap();
  if (subject_job.value(QStringLiteral("status")).toString() != QStringLiteral("Completed")) {
    return fail("subject mask export should complete successfully");
  }

  QImage subject_rendered_image(subject_job.value(QStringLiteral("outputPath")).toString());
  if (subject_rendered_image.isNull()) {
    return fail("subject mask export should produce a readable image");
  }

  const auto left_sample = subject_rendered_image.pixelColor(subject_rendered_image.width() / 4, subject_rendered_image.height() / 2);
  const auto right_sample = subject_rendered_image.pixelColor(subject_rendered_image.width() * 3 / 4, subject_rendered_image.height() / 2);
  if (!(left_sample.red() > right_sample.red() + 20 &&
        left_sample.green() > right_sample.green() + 20 &&
        left_sample.blue() > right_sample.blue() + 20)) {
    return fail("subject mask export should apply local adjustments to the segmented region");
  }

  const auto removal_source_path = temp_dir.filePath(QStringLiteral("removal-source.png"));
  QImage removal_source(33, 33, QImage::Format_ARGB32);
  removal_source.fill(QColor(96, 96, 96));
  removal_source.setPixelColor(16, 16, QColor(240, 32, 32));
  if (!removal_source.save(removal_source_path)) {
    return fail("removal source image should be written for export testing");
  }

  photo_editor::desktop::EditRecipeState removal_state;
  QVariantMap removal_mask;
  removal_mask.insert(QStringLiteral("id"), QStringLiteral("remove-export"));
  removal_mask.insert(QStringLiteral("name"), QStringLiteral("Remove Object"));
  removal_mask.insert(QStringLiteral("kind"), QStringLiteral("object"));
  removal_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  removal_mask.insert(QStringLiteral("enabled"), true);
  removal_mask.insert(QStringLiteral("removeStrength"), 100.0);
  removal_mask.insert(QStringLiteral("feather"), 0.05);
  QByteArray removal_segmentation(33 * 33, '\0');
  removal_segmentation[16 * 33 + 16] = static_cast<char>(0xff);
  removal_mask.insert(QStringLiteral("segmentationData"), removal_segmentation);
  removal_mask.insert(QStringLiteral("segmentationWidth"), 33);
  removal_mask.insert(QStringLiteral("segmentationHeight"), 33);
  removal_state.mask_stack = {removal_mask};
  const auto removal_recipe_json = photo_editor::desktop::serialize_edit_recipe_json(removal_state);

  const auto removal_job_index = controller.jobCount();
  if (!controller.queueExport(
          QStringLiteral("asset-remove"),
          QStringLiteral("photo"),
          QStringLiteral("Removal Export"),
          removal_source_path,
          QString(),
          removal_recipe_json,
          QStringLiteral("Full Resolution"),
          export_dir)) {
    return fail("removal export should queue successfully");
  }

  if (!wait_for_job(&controller, removal_job_index)) {
    return fail("removal export should finish");
  }

  const auto removal_job = controller.jobs().at(removal_job_index).toMap();
  if (removal_job.value(QStringLiteral("status")).toString() != QStringLiteral("Completed")) {
    return fail("removal export should complete successfully");
  }

  QImage removal_rendered_image(removal_job.value(QStringLiteral("outputPath")).toString());
  if (removal_rendered_image.isNull()) {
    return fail("removal export should produce a readable image");
  }

  const auto removal_center = removal_rendered_image.pixelColor(16, 16);
  if (!(removal_center.green() > 70 && removal_center.blue() > 70 && removal_center.red() < 180)) {
    return fail("removal export should fill the removed object from surrounding pixels");
  }

  return EXIT_SUCCESS;
}
