#include <algorithm>
#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QColorSpace>
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

bool issue_code_present(const QVariantList& issues, const QString& code) {
  for (const auto& value : issues) {
    if (value.toMap().value(QStringLiteral("code")).toString() == code) {
      return true;
    }
  }
  return false;
}

int max_channel_delta(const QImage& left, const QImage& right) {
  if (left.size() != right.size()) {
    return 255;
  }

  auto max_delta = 0;
  const auto left_argb = left.convertToFormat(QImage::Format_ARGB32);
  const auto right_argb = right.convertToFormat(QImage::Format_ARGB32);
  for (int y = 0; y < left_argb.height(); ++y) {
    for (int x = 0; x < left_argb.width(); ++x) {
      const auto lhs = left_argb.pixelColor(x, y);
      const auto rhs = right_argb.pixelColor(x, y);
      max_delta = std::max(max_delta, std::abs(lhs.red() - rhs.red()));
      max_delta = std::max(max_delta, std::abs(lhs.green() - rhs.green()));
      max_delta = std::max(max_delta, std::abs(lhs.blue() - rhs.blue()));
    }
  }
  return max_delta;
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
  crop_state.output_color_space = QStringLiteral("Display P3");
  const auto recipe_json = photo_editor::desktop::serialize_edit_recipe_json(crop_state);

  photo_editor::desktop::JobRunner runner;
  photo_editor::desktop::ExportJobController controller;
  photo_editor::desktop::EditRecipeState preflight_state;
  preflight_state.output_color_space = QStringLiteral("Display P3");
  preflight_state.soft_proof_enabled = true;
  preflight_state.soft_proof_profile = QStringLiteral("sRGB");
  preflight_state.crop_rect_width = 0.5;
  preflight_state.crop_rect_height = 1.0;
  const auto preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      source_path,
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(preflight_state),
      QStringLiteral("Web Proof"),
      export_dir,
      100,
      3,
      4,
      80,
      1,
      0);
  const auto preflight_issues = preflight.value(QStringLiteral("issues")).toList();
  if (!preflight.value(QStringLiteral("ok")).toBool() ||
      preflight.value(QStringLiteral("warningCount")).toInt() < 4 ||
      preflight.value(QStringLiteral("outputColorSpace")).toString() != QStringLiteral("Display P3") ||
      preflight.value(QStringLiteral("softProofProfile")).toString() != QStringLiteral("sRGB") ||
      preflight.value(QStringLiteral("estimatedPixelWidth")).toInt() != 40 ||
      !issue_code_present(preflight_issues, QStringLiteral("profile-web-wide-gamut")) ||
      !issue_code_present(preflight_issues, QStringLiteral("soft-proof-mismatch")) ||
      !issue_code_present(preflight_issues, QStringLiteral("preview-clipping")) ||
      !issue_code_present(preflight_issues, QStringLiteral("raw-clipping"))) {
    return fail("export preflight should catch profile, soft-proof, clipping, and size risks before queueing");
  }

  const auto missing_source_preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      temp_dir.filePath(QStringLiteral("missing.png")),
      QString(),
      recipe_json,
      QStringLiteral("Full Resolution"),
      export_dir,
      0,
      0,
      0,
      0,
      0,
      0);
  if (missing_source_preflight.value(QStringLiteral("ok")).toBool() ||
      !issue_code_present(
          missing_source_preflight.value(QStringLiteral("issues")).toList(),
          QStringLiteral("source-missing"))) {
    return fail("export preflight should block missing rendered photo sources");
  }

  const auto missing_destination_preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      source_path,
      QString(),
      recipe_json,
      QStringLiteral("Full Resolution"),
      QString(),
      0,
      0,
      0,
      0,
      0,
      0);
  if (missing_destination_preflight.value(QStringLiteral("ok")).toBool() ||
      !issue_code_present(
          missing_destination_preflight.value(QStringLiteral("issues")).toList(),
          QStringLiteral("destination-missing"))) {
    return fail("export preflight should block missing destinations");
  }

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
  if (rendered_info.suffix().toLower() != QStringLiteral("png")) {
    return fail("full-resolution photo export should write a png");
  }

  QImage rendered_image(rendered_path);
  if (rendered_image.isNull()) {
    return fail("rendered photo export should produce a readable image");
  }
  if (rendered_image.width() != 40 || rendered_image.height() != 40) {
    return fail("rendered photo export should honor the crop dimensions");
  }
  if (!rendered_image.colorSpace().isValid()) {
    return fail("rendered photo export should preserve an output color profile");
  }
  if (rendered_image.colorSpace() != QColorSpace(QColorSpace::DisplayP3)) {
    return fail("rendered photo export should embed the requested Display P3 output profile");
  }
  if (rendered_image.format() != QImage::Format_RGBA64) {
    return fail("full-resolution rendered PNG export should preserve a 16-bit image format");
  }

  const auto sample = rendered_image.pixelColor(rendered_image.width() / 2, rendered_image.height() / 2);
  if (sample.blue() < 160 || sample.red() > 120) {
    return fail("rendered photo export should contain the cropped right-half content");
  }

  const auto crop_bounds_source_path = temp_dir.filePath(QStringLiteral("crop-bounds-source.png"));
  QImage crop_bounds_source(11, 7, QImage::Format_ARGB32);
  crop_bounds_source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < crop_bounds_source.height(); ++y) {
    for (int x = 0; x < crop_bounds_source.width(); ++x) {
      crop_bounds_source.setPixelColor(
          x,
          y,
          QColor(17 + x * 19, 23 + y * 27, 31 + x * 7 + y * 9, 255));
    }
  }
  if (!crop_bounds_source.save(crop_bounds_source_path)) {
    return fail("crop bounds source image should be written for export testing");
  }
  photo_editor::desktop::EditRecipeState crop_bounds_state;
  crop_bounds_state.crop_rect_x = 2.0 / 11.0;
  crop_bounds_state.crop_rect_y = 1.0 / 7.0;
  crop_bounds_state.crop_rect_width = 6.0 / 11.0;
  crop_bounds_state.crop_rect_height = 4.0 / 7.0;
  crop_bounds_state.output_color_space = QStringLiteral("sRGB");
  const auto expected_crop_rect =
      photo_editor::desktop::crop_rect_for_image(crop_bounds_source.size(), crop_bounds_state);
  const auto expected_crop_preview = photo_editor::desktop::render_output_image(
      photo_editor::desktop::apply_crop(crop_bounds_source, crop_bounds_state),
      crop_bounds_state,
      QColorSpace(QColorSpace::SRgb),
      QImage::Format_RGBA64);
  if (expected_crop_rect != QRect(2, 1, 6, 4) || expected_crop_preview.size() != expected_crop_rect.size()) {
    return fail("preview crop bounds proof should establish the exact expected crop rectangle");
  }
  const auto crop_bounds_job_index = controller.jobCount();
  if (!controller.queueExport(
          QStringLiteral("asset-crop-bounds"),
          QStringLiteral("photo"),
          QStringLiteral("Crop Bounds Check"),
          crop_bounds_source_path,
          QString(),
          photo_editor::desktop::serialize_edit_recipe_json(crop_bounds_state),
          QStringLiteral("Full Resolution"),
          export_dir)) {
    return fail("crop bounds export should queue successfully");
  }
  if (!wait_for_job(&controller, crop_bounds_job_index)) {
    return fail("crop bounds export should finish");
  }
  const auto crop_bounds_job = controller.jobs().at(crop_bounds_job_index).toMap();
  const QImage crop_bounds_exported(crop_bounds_job.value(QStringLiteral("outputPath")).toString());
  if (crop_bounds_exported.size() != expected_crop_rect.size()) {
    return fail("export crop bounds should match the preview crop rectangle size exactly");
  }
  if (max_channel_delta(expected_crop_preview, crop_bounds_exported) > 1) {
    return fail("export crop result should match the preview crop bounds and pixels exactly");
  }
  const auto crop_bounds_diff = crop_bounds_job.value(QStringLiteral("previewExportDiff")).toMap();
  if (!crop_bounds_diff.value(QStringLiteral("available")).toBool()) {
    return fail("export crop proof should publish preview/export diff metadata");
  }
  if (crop_bounds_diff.value(QStringLiteral("width")).toInt() != expected_crop_rect.width() ||
      crop_bounds_diff.value(QStringLiteral("height")).toInt() != expected_crop_rect.height()) {
    return fail("export crop proof diff dimensions should match the exact preview crop bounds");
  }
  if (!crop_bounds_diff.value(QStringLiteral("withinTolerance")).toBool() ||
      crop_bounds_diff.value(QStringLiteral("maxChannelDelta")).toInt() > 1 ||
      crop_bounds_diff.value(QStringLiteral("tolerance")).toInt() != 1) {
    return fail("export crop proof diff should verify written pixels are within lossless tolerance");
  }
  const auto crop_bounds_metadata = crop_bounds_job.value(QStringLiteral("metadata")).toMap();
  if (crop_bounds_metadata.value(QStringLiteral("previewExportDiff")).toMap().isEmpty()) {
    return fail("export crop proof diff should persist in job metadata");
  }

  const auto original_constraint_job_index = controller.jobCount();
  if (!controller.queueExportWithCropConstraint(
          QStringLiteral("asset-original-constraint"),
          QStringLiteral("photo"),
          QStringLiteral("Original Constraint"),
          source_path,
          QString(),
          recipe_json,
          QStringLiteral("Full Resolution"),
          export_dir,
          QStringLiteral("Original"),
          0.0,
          0.0,
          0.0,
          0.0)) {
    return fail("export crop constraints should queue successfully");
  }
  const auto original_constraint_metadata =
      controller.jobs().at(original_constraint_job_index).toMap().value(QStringLiteral("exportCrop")).toMap();
  if (original_constraint_metadata.value(QStringLiteral("preset")).toString() != QStringLiteral("Original")) {
    return fail("export crop constraints should be visible in queued job metadata");
  }
  if (!wait_for_job(&controller, original_constraint_job_index)) {
    return fail("export crop constraint job should finish");
  }
  const QImage original_constraint_image(
      controller.jobs()
          .at(original_constraint_job_index)
          .toMap()
          .value(QStringLiteral("outputPath"))
          .toString());
  if (original_constraint_image.width() != 80 || original_constraint_image.height() != 40) {
    return fail("export crop constraints should be able to clear the recipe crop for one export");
  }

  const auto square_constraint_job_index = controller.jobCount();
  if (!controller.queueExportWithCropConstraint(
          QStringLiteral("asset-square-constraint"),
          QStringLiteral("photo"),
          QStringLiteral("Square Constraint"),
          source_path,
          QString(),
          QString(),
          QStringLiteral("Full Resolution"),
          export_dir,
          QStringLiteral("1:1"),
          0.0,
          0.0,
          0.0,
          0.0)) {
    return fail("square export crop constraints should queue successfully");
  }
  if (!wait_for_job(&controller, square_constraint_job_index)) {
    return fail("square export crop constraint job should finish");
  }
  const QImage square_constraint_image(
      controller.jobs()
          .at(square_constraint_job_index)
          .toMap()
          .value(QStringLiteral("outputPath"))
          .toString());
  if (square_constraint_image.width() != 40 || square_constraint_image.height() != 40) {
    return fail("export crop constraints should apply an export-only crop preset");
  }

  const auto archive_job_index = controller.jobCount();
  if (!controller.queueExport(
          QStringLiteral("asset-archive"),
          QStringLiteral("photo"),
          QStringLiteral("Archive Check"),
          source_path,
          QString(),
          recipe_json,
          QStringLiteral("Archive TIFF"),
          export_dir)) {
    return fail("archive TIFF export should queue successfully");
  }
  if (!wait_for_job(&controller, archive_job_index)) {
    return fail("archive TIFF export should finish");
  }
  const auto archive_job = controller.jobs().at(archive_job_index).toMap();
  if (archive_job.value(QStringLiteral("status")).toString() != QStringLiteral("Completed")) {
    return fail("archive TIFF export should complete successfully");
  }
  const auto archive_path = archive_job.value(QStringLiteral("outputPath")).toString();
  if (QFileInfo(archive_path).suffix().toLower() != QStringLiteral("tif")) {
    return fail("archive export should write a TIFF artifact");
  }
  const QImage archive_image(archive_path);
  if (archive_image.isNull()) {
    return fail("archive TIFF export should produce a readable image");
  }
  if (archive_image.format() != QImage::Format_RGBA64) {
    return fail("archive TIFF export should preserve a 16-bit image format");
  }
  if (archive_image.colorSpace() != QColorSpace(QColorSpace::DisplayP3)) {
    return fail("archive TIFF export should embed the requested output profile");
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
