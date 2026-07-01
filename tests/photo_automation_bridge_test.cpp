#include "photo_automation_bridge.h"

#include "agent_edit_controller.h"
#include "app_bootstrap.h"
#include "catalog_asset_model.h"
#include "edit_recipe_state.h"
#include "export_job_controller.h"

#include <cstdlib>
#include <iostream>

#include <QColor>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTime>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

photo_editor::desktop::CatalogAssetItem make_asset(
    const QString& asset_id,
    const QString& file_path,
    const QString& recipe_json) {
  photo_editor::desktop::CatalogAssetItem item;
  item.asset_id = asset_id;
  item.camera_label = QStringLiteral("Test Camera");
  item.file_path = file_path;
  item.capture_date = QStringLiteral("Jun 29, 2026");
  item.capture_date_value = QDate(2026, 6, 29);
  item.lens_model = QStringLiteral("RF 50mm F1.2L USM");
  item.iso_label = QStringLiteral("ISO 200");
  item.shutter_speed = QStringLiteral("1/500 s");
  item.aperture_label = QStringLiteral("f/2");
  item.imported_at = QStringLiteral("Jun 29, 2026  1:00 PM");
  item.imported_at_value = QDateTime(QDate(2026, 6, 29), QTime(13, 0));
  item.media_type = QStringLiteral("photo");
  item.recipe_json = recipe_json;
  item.online = true;
  return item;
}

bool write_fixture_image(const QString& file_path) {
  QImage image(20, 20, QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.setPixelColor(x, y, QColor(88 + x, 104 + y, 118));
    }
  }
  const QFileInfo info(file_path);
  if (!QDir().mkpath(info.dir().absolutePath())) {
    return false;
  }
  return image.save(file_path, "PNG");
}

QJsonObject read_json_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QJsonDocument::fromJson(file.readAll()).object();
}

QJsonObject send_local_request(const QString& endpoint, const QJsonObject& request) {
  QLocalSocket socket;
  socket.connectToServer(endpoint);
  if (!socket.waitForConnected(1000)) {
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), socket.errorString()}};
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
  socket.write("\n");
  socket.flush();
  socket.waitForBytesWritten(1000);

  QByteArray response;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 3000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (socket.waitForReadyRead(10)) {
      response.append(socket.readAll());
      if (response.contains('\n')) {
        break;
      }
    }
  }
  return QJsonDocument::fromJson(response).object();
}

QJsonObject send_tcp_request(const QString& endpoint, const QJsonObject& request) {
  const auto parts = endpoint.split(QStringLiteral(":"));
  if (parts.size() != 2) {
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("invalid endpoint")}};
  }
  auto ok = false;
  const auto port = parts.at(1).toUShort(&ok);
  if (!ok) {
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("invalid port")}};
  }

  QTcpSocket socket;
  socket.connectToHost(parts.at(0), port);
  if (!socket.waitForConnected(1000)) {
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), socket.errorString()}};
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
  socket.write("\n");
  socket.flush();
  socket.waitForBytesWritten(1000);

  QByteArray response;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 3000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (socket.waitForReadyRead(10)) {
      response.append(socket.readAll());
      if (response.contains('\n')) {
        break;
      }
    }
  }
  return QJsonDocument::fromJson(response).object();
}

QJsonObject send_request(
    const photo_editor::desktop::PhotoAutomationBridge& bridge,
    const QJsonObject& session,
    const QJsonObject& request) {
  if (session.value(QStringLiteral("endpointKind")).toString() == QStringLiteral("in-memory")) {
    return bridge.handleJsonRequest(request);
  }
  const auto kind = session.value(QStringLiteral("endpointKind")).toString();
  const auto endpoint = session.value(QStringLiteral("endpoint")).toString();
  if (kind == QStringLiteral("tcp-localhost")) {
    const auto response = send_tcp_request(endpoint, request);
    return response.isEmpty() ? bridge.handleJsonRequest(request) : response;
  }
  const auto response = send_local_request(endpoint, request);
  return response.isEmpty() ? bridge.handleJsonRequest(request) : response;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("PhotoAutomationBridgeTest"));

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("temporary runtime directory should be created");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());

  const auto source_path = QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/source.png"));
  if (!write_fixture_image(source_path)) {
    return fail("fixture image should be written");
  }
  const auto second_source_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/source-2.png"));
  if (!write_fixture_image(second_source_path)) {
    return fail("second fixture image should be written");
  }

  photo_editor::desktop::EditRecipeState original_state;
  original_state.output_color_space = QStringLiteral("Display P3");
  const auto original_recipe = photo_editor::desktop::serialize_edit_recipe_json(original_state);

  photo_editor::desktop::CatalogAssetModel catalog_model;
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);

  photo_editor::desktop::AgentEditController agent_edit;
  photo_editor::desktop::ExportJobController export_jobs;
  agent_edit.setCatalogModel(&catalog_model);
  agent_edit.setExportJobController(&export_jobs);

  photo_editor::desktop::PhotoAutomationBridge bridge(&agent_edit, &catalog_model);
  QJsonObject session;
  if (bridge.start()) {
    session = read_json_file(bridge.sessionFilePath());
    if (session.value(QStringLiteral("endpoint")).toString() != bridge.endpointName() ||
        session.value(QStringLiteral("endpointKind")).toString() != bridge.endpointKind() ||
        session.value(QStringLiteral("token")).toString() != bridge.token()) {
      return fail("session file should expose endpoint and token");
    }
    session.insert(QStringLiteral("endpointKind"), QStringLiteral("in-memory"));
    session.insert(QStringLiteral("endpoint"), QStringLiteral("in-memory"));
  } else {
    const auto error = bridge.errorMessage();
    if (!error.contains(QStringLiteral("listen"), Qt::CaseInsensitive)) {
      std::cerr << error.toStdString() << '\n';
      return fail("automation bridge should start or be blocked only by listener restrictions");
    }
    session = QJsonObject{
        {QStringLiteral("endpointKind"), QStringLiteral("in-memory")},
        {QStringLiteral("endpoint"), QStringLiteral("in-memory")},
        {QStringLiteral("token"), bridge.token()},
    };
  }

  const auto invalid_token = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), QStringLiteral("wrong")},
          {QStringLiteral("command"), QStringLiteral("photo_live_status")},
      });
  if (invalid_token.value(QStringLiteral("ok")).toBool()) {
    return fail("bridge should reject invalid tokens");
  }

  const auto unknown_field = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_live_status")},
          {QStringLiteral("unexpected"), true},
      });
  if (unknown_field.value(QStringLiteral("ok")).toBool()) {
    return fail("bridge should reject unknown top-level fields");
  }

  const auto status = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_live_status")},
      });
  if (!status.value(QStringLiteral("ok")).toBool() ||
      status.value(QStringLiteral("result")).toObject().value(QStringLiteral("selectedAssetCount")).toInt() != 1) {
    return fail("photo_live_status should report active selection");
  }

  const auto selected_asset = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_read_asset")},
      });
  const auto selected_asset_result = selected_asset.value(QStringLiteral("result")).toObject();
  if (!selected_asset.value(QStringLiteral("ok")).toBool() ||
      selected_asset_result.value(QStringLiteral("assetId")).toString() != QStringLiteral("asset-1") ||
      selected_asset_result.value(QStringLiteral("asset")).toObject().value(QStringLiteral("filePath")).toString() !=
          source_path) {
    return fail("photo_read_asset should return selected asset metadata");
  }

  const auto proof = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_render_proof")},
      });
  const auto proof_result = proof.value(QStringLiteral("result")).toObject();
  const auto proof_path = proof_result.value(QStringLiteral("currentProofPath")).toString();
  if (!proof.value(QStringLiteral("ok")).toBool() ||
      !proof_result.value(QStringLiteral("ok")).toBool() ||
      proof_result.value(QStringLiteral("mode")).toString() != QStringLiteral("render-proof") ||
      !QFileInfo::exists(proof_path) ||
      agent_edit.draftActive()) {
    return fail("photo_render_proof should write a read-only proof artifact for the selected asset");
  }

  const auto validate_edit = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_validate_edit")},
      });
  const auto validate_edit_result = validate_edit.value(QStringLiteral("result")).toObject();
  if (!validate_edit.value(QStringLiteral("ok")).toBool() ||
      !validate_edit_result.value(QStringLiteral("ok")).toBool() ||
      validate_edit_result.value(QStringLiteral("mode")).toString() != QStringLiteral("validate-edit") ||
      !validate_edit_result.value(QStringLiteral("validationReport")).toObject().value(QStringLiteral("ok")).toBool() ||
      !QFileInfo::exists(validate_edit_result.value(QStringLiteral("currentProofPath")).toString()) ||
      agent_edit.draftActive()) {
    return fail("photo_validate_edit should validate the selected edit without creating a draft");
  }

  const auto export_dir = QDir(runtime_root.path()).filePath(QStringLiteral("exports"));
  if (!QDir().mkpath(export_dir)) {
    return fail("export destination should be created for bridge preflight testing");
  }
  const auto export_preflight = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_export_preflight")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("preset"), QStringLiteral("Web Proof")},
               {QStringLiteral("destinationDirectory"), export_dir},
               {QStringLiteral("histogramSampleCount"), 100},
               {QStringLiteral("highlightClipCount"), 2},
           }},
      });
  const auto export_preflight_result = export_preflight.value(QStringLiteral("result")).toObject();
  if (!export_preflight.value(QStringLiteral("ok")).toBool() ||
      !export_preflight_result.value(QStringLiteral("ok")).toBool() ||
      export_preflight_result.value(QStringLiteral("mode")).toString() != QStringLiteral("export-preflight") ||
      export_preflight_result.value(QStringLiteral("checkedCount")).toInt() != 1 ||
      export_preflight_result.value(QStringLiteral("warningCount")).toInt() < 2 ||
      !QFileInfo::exists(export_preflight_result.value(QStringLiteral("preflightReportPath")).toString()) ||
      agent_edit.draftActive()) {
    return fail("photo_export_preflight should return and persist selected asset export readiness without creating a draft");
  }
  const auto malformed_preflight = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_export_preflight")},
          {QStringLiteral("args"),
           QJsonObject{{QStringLiteral("histogramSampleCount"), QStringLiteral("many")}}},
      });
  if (malformed_preflight.value(QStringLiteral("ok")).toBool()) {
    return fail("photo_export_preflight should reject malformed metric args");
  }

  const auto unconfirmed_export = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_export_batch")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("confirmed"), false},
               {QStringLiteral("preset"), QStringLiteral("Web Proof")},
               {QStringLiteral("destinationDirectory"), export_dir},
           }},
      });
  if (!unconfirmed_export.value(QStringLiteral("ok")).toBool() ||
      unconfirmed_export.value(QStringLiteral("result")).toObject().value(QStringLiteral("ok")).toBool() ||
      export_jobs.jobCount() != 0) {
    return fail("photo_export_batch should require explicit confirmation before queueing jobs");
  }
  const auto confirmed_export = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_export_batch")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("confirmed"), true},
               {QStringLiteral("preset"), QStringLiteral("Web Proof")},
               {QStringLiteral("destinationDirectory"), export_dir},
               {QStringLiteral("histogramSampleCount"), 100},
               {QStringLiteral("highlightClipCount"), 2},
           }},
      });
  const auto confirmed_export_result = confirmed_export.value(QStringLiteral("result")).toObject();
  if (!confirmed_export.value(QStringLiteral("ok")).toBool() ||
      !confirmed_export_result.value(QStringLiteral("ok")).toBool() ||
      confirmed_export_result.value(QStringLiteral("queuedCount")).toInt() != 1 ||
      export_jobs.jobCount() != 1 ||
      agent_edit.draftActive()) {
    return fail("photo_export_batch should queue confirmed selected-asset exports");
  }

  const auto malformed_patch = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_validate_recipe_patch")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("patch"), QJsonObject{}},
               {QStringLiteral("extra"), true},
           }},
      });
  if (malformed_patch.value(QStringLiteral("ok")).toBool()) {
    return fail("bridge should reject unknown command args");
  }

  const auto linear_mask = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_create_mask")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("type"), QStringLiteral("linear")},
               {QStringLiteral("position"), 0.42},
               {QStringLiteral("rotation"), -12.0},
               {QStringLiteral("exposure"), 0.3},
               {QStringLiteral("feather"), 0.35},
           }},
      });
  const auto linear_mask_result = linear_mask.value(QStringLiteral("result")).toObject();
  const auto linear_mask_patch = linear_mask_result.value(QStringLiteral("maskPatch")).toObject();
  const auto linear_mask_values = linear_mask_patch.value(QStringLiteral("masks")).toObject();
  const auto linear_state = photo_editor::desktop::parse_edit_recipe_json(catalog_model.selectedRecipeJson());
  if (!linear_mask.value(QStringLiteral("ok")).toBool() ||
      !linear_mask_result.value(QStringLiteral("ok")).toBool() ||
      linear_mask_result.value(QStringLiteral("mode")).toString() != QStringLiteral("photo_create_mask") ||
      linear_mask_result.value(QStringLiteral("maskType")).toString() != QStringLiteral("linear") ||
      linear_mask_values.value(QStringLiteral("linearMaskExposure")).toDouble() != 0.3 ||
      linear_state.mask_stack.empty() ||
      !agent_edit.draftActive()) {
    return fail("photo_create_mask should create an active linear mask draft");
  }
  const auto rollback_linear_mask = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_rollback_agent_draft")},
      });
  if (!rollback_linear_mask.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
    return fail("bridge should roll back a mask creation draft");
  }

  const auto radial_mask = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_apply_mask_adjustment")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("type"), QStringLiteral("radial")},
               {QStringLiteral("centerX"), 0.52},
               {QStringLiteral("centerY"), 0.48},
               {QStringLiteral("radiusX"), 0.3},
               {QStringLiteral("radiusY"), 0.28},
               {QStringLiteral("exposure"), -0.2},
               {QStringLiteral("temperature"), 8.0},
           }},
      });
  const auto radial_mask_result = radial_mask.value(QStringLiteral("result")).toObject();
  const auto radial_mask_patch = radial_mask_result.value(QStringLiteral("maskPatch")).toObject();
  const auto radial_mask_values = radial_mask_patch.value(QStringLiteral("masks")).toObject();
  const auto radial_state = photo_editor::desktop::parse_edit_recipe_json(catalog_model.selectedRecipeJson());
  if (!radial_mask.value(QStringLiteral("ok")).toBool() ||
      !radial_mask_result.value(QStringLiteral("ok")).toBool() ||
      radial_mask_result.value(QStringLiteral("mode")).toString() != QStringLiteral("photo_apply_mask_adjustment") ||
      radial_mask_result.value(QStringLiteral("maskType")).toString() != QStringLiteral("radial") ||
      radial_mask_values.value(QStringLiteral("radialMaskExposure")).toDouble() != -0.2 ||
      radial_mask_values.value(QStringLiteral("radialMaskTemperature")).toDouble() != 8.0 ||
      radial_state.mask_stack.empty() ||
      !agent_edit.draftActive()) {
    return fail("photo_apply_mask_adjustment should create an active radial mask adjustment draft");
  }
  const auto rollback_radial_mask = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_rollback_agent_draft")},
      });
  if (!rollback_radial_mask.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
    return fail("bridge should roll back a mask adjustment draft");
  }

  const auto malformed_mask = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_apply_mask_adjustment")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("type"), QStringLiteral("radial")},
               {QStringLiteral("radiusX"), 2.0},
           }},
      });
  if (malformed_mask.value(QStringLiteral("ok")).toBool()) {
    return fail("photo_apply_mask_adjustment should reject out-of-range mask fields");
  }

  const auto single_asset_bulk = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_plan_bulk_edit")},
          {QStringLiteral("args"),
           QJsonObject{{QStringLiteral("instructions"), QStringLiteral("consistent editorial batch")}}},
      });
  if (single_asset_bulk.value(QStringLiteral("ok")).toBool()) {
    return fail("photo_plan_bulk_edit should require a multi-asset selection");
  }

  catalog_model.toggleSelectionIndex(1);
  const auto bulk_plan = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_plan_bulk_edit")},
          {QStringLiteral("args"),
           QJsonObject{{QStringLiteral("instructions"), QStringLiteral("consistent editorial batch")}}},
      });
  const auto bulk_plan_result = bulk_plan.value(QStringLiteral("result")).toObject();
  if (!bulk_plan.value(QStringLiteral("ok")).toBool() ||
      !bulk_plan_result.value(QStringLiteral("ok")).toBool() ||
      bulk_plan_result.value(QStringLiteral("automationCommand")).toString() !=
          QStringLiteral("photo_plan_bulk_edit") ||
      bulk_plan_result.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      bulk_plan_result.value(QStringLiteral("assetIds")).toArray().size() != 2 ||
      bulk_plan_result.value(QStringLiteral("recipePatch")).toObject().isEmpty() ||
      agent_edit.draftActive()) {
    return fail("photo_plan_bulk_edit should plan a selected-set draft without mutating recipes");
  }

  const auto recipe_before_batch_validation = catalog_model.selectedRecipeJson();
  const auto batch_validation = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_validate_batch")},
      });
  const auto batch_validation_result = batch_validation.value(QStringLiteral("result")).toObject();
  if (!batch_validation.value(QStringLiteral("ok")).toBool() ||
      !batch_validation_result.value(QStringLiteral("ok")).toBool() ||
      batch_validation_result.value(QStringLiteral("mode")).toString() != QStringLiteral("validate-batch") ||
      batch_validation_result.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      batch_validation_result.value(QStringLiteral("checkedCount")).toInt() != 2 ||
      batch_validation_result.value(QStringLiteral("assetReports")).toArray().size() != 2 ||
      !QFileInfo::exists(batch_validation_result.value(QStringLiteral("batchValidationReportPath")).toString()) ||
      agent_edit.draftActive() ||
      catalog_model.selectedRecipeJson() != recipe_before_batch_validation) {
    return fail("photo_validate_batch should validate selected assets without mutating recipes");
  }

  const auto bulk_draft = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_apply_bulk_draft")},
          {QStringLiteral("args"),
           QJsonObject{{QStringLiteral("instructions"), QStringLiteral("consistent editorial batch")}}},
      });
  const auto bulk_draft_result = bulk_draft.value(QStringLiteral("result")).toObject();
  if (!bulk_draft.value(QStringLiteral("ok")).toBool() ||
      !bulk_draft_result.value(QStringLiteral("ok")).toBool() ||
      bulk_draft_result.value(QStringLiteral("automationCommand")).toString() !=
          QStringLiteral("photo_apply_bulk_draft") ||
      bulk_draft_result.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      bulk_draft_result.value(QStringLiteral("draftedCount")).toInt() != 2 ||
      bulk_draft_result.value(QStringLiteral("assetReports")).toArray().size() != 2 ||
      !agent_edit.draftActive()) {
    return fail("photo_apply_bulk_draft should create a reversible selected-set draft");
  }
  const auto rollback_bulk = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_rollback_agent_draft")},
      });
  if (!rollback_bulk.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
    return fail("bridge should roll back a bulk draft");
  }

  const auto draft = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_run_draft_on_selected")},
          {QStringLiteral("args"),
           QJsonObject{{QStringLiteral("instructions"), QStringLiteral("warm bright editorial draft")}}},
      });
  if (!draft.value(QStringLiteral("ok")).toBool() ||
      !draft.value(QStringLiteral("result")).toObject().value(QStringLiteral("ok")).toBool() ||
      !agent_edit.draftActive()) {
    return fail("bridge should run an authenticated agent draft");
  }

  const auto unconfirmed_commit = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_commit_agent_draft")},
          {QStringLiteral("args"), QJsonObject{{QStringLiteral("confirmed"), false}}},
      });
  if (unconfirmed_commit.value(QStringLiteral("ok")).toBool() || !agent_edit.draftActive()) {
    return fail("bridge should reject unconfirmed agent draft commits without clearing the draft");
  }

  const auto rollback = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_rollback_agent_draft")},
      });
  if (!rollback.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
    return fail("bridge should roll back an authenticated agent draft");
  }

  const QJsonObject retouch_patch{
      {QStringLiteral("retouch"),
       QJsonObject{
           {QStringLiteral("retouchStack"),
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("bridge-heal-retouch")},
                    {QStringLiteral("name"), QStringLiteral("Bridge Heal Retouch")},
                    {QStringLiteral("kind"), QStringLiteral("brush")},
                    {QStringLiteral("retouchMode"), QStringLiteral("heal")},
                    {QStringLiteral("brushPoints"),
                     QJsonArray{
                         QJsonObject{
                             {QStringLiteral("x"), 0.42},
                             {QStringLiteral("y"), 0.48},
                             {QStringLiteral("size"), 0.08},
                             {QStringLiteral("flow"), 0.72},
                         },
                     }},
                },
            }},
       }},
  };
  const auto unconfirmed_retouch = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_apply_recipe_patch_draft")},
          {QStringLiteral("args"), QJsonObject{{QStringLiteral("patch"), retouch_patch}}},
      });
  if (unconfirmed_retouch.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
    return fail("bridge should require explicit confirmation before applying retouch patches");
  }
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  const auto confirmed_retouch = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_apply_recipe_patch_draft")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("patch"), retouch_patch},
               {QStringLiteral("confirmed"), true},
           }},
      });
  if (!confirmed_retouch.value(QStringLiteral("ok")).toBool() ||
      !confirmed_retouch.value(QStringLiteral("result")).toObject().value(QStringLiteral("ok")).toBool() ||
      !agent_edit.draftActive()) {
    return fail("bridge should apply confirmed retouch patches as reversible drafts");
  }
  const auto rollback_retouch = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_rollback_agent_draft")},
      });
  if (!rollback_retouch.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
    return fail("bridge should roll back confirmed retouch drafts");
  }
  catalog_model.toggleSelectionIndex(1);

  const auto commit_draft = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_run_draft_on_selected")},
          {QStringLiteral("args"),
           QJsonObject{{QStringLiteral("instructions"), QStringLiteral("warm bright editorial draft")}}},
      });
  if (!commit_draft.value(QStringLiteral("ok")).toBool() || !agent_edit.draftActive()) {
    return fail("bridge should create a draft for confirmed commit testing");
  }
  const auto confirmed_commit = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_commit_agent_draft")},
          {QStringLiteral("args"), QJsonObject{{QStringLiteral("confirmed"), true}}},
      });
  const auto confirmed_commit_result = confirmed_commit.value(QStringLiteral("result")).toObject();
  if (!confirmed_commit.value(QStringLiteral("ok")).toBool() ||
      !confirmed_commit_result.value(QStringLiteral("ok")).toBool() ||
      confirmed_commit_result.value(QStringLiteral("mode")).toString() != QStringLiteral("commit") ||
      confirmed_commit_result.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      confirmed_commit_result.value(QStringLiteral("committedCount")).toInt() != 2 ||
      confirmed_commit_result.value(QStringLiteral("assetIds")).toArray().size() != 2 ||
      agent_edit.draftActive()) {
    return fail("bridge should commit a selected-set agent draft only after explicit confirmation");
  }

  const auto empty_commit = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_commit_agent_draft")},
          {QStringLiteral("args"), QJsonObject{{QStringLiteral("confirmed"), true}}},
      });
  if (!empty_commit.value(QStringLiteral("ok")).toBool() ||
      empty_commit.value(QStringLiteral("result")).toObject().value(QStringLiteral("ok")).toBool()) {
    return fail("bridge should not commit when no agent draft is active");
  }

  const auto idle_cancel = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_cancel_agent_run")},
          {QStringLiteral("args"), QJsonObject{{QStringLiteral("reason"), QStringLiteral("test_idle")}}},
      });
  if (!idle_cancel.value(QStringLiteral("ok")).toBool() ||
      idle_cancel.value(QStringLiteral("result")).toObject().value(QStringLiteral("ok")).toBool()) {
    return fail("bridge cancel command should report no active run while idle");
  }

  bridge.stop();
  if (QFileInfo::exists(bridge.sessionFilePath())) {
    return fail("stopping bridge should remove session file");
  }

  return EXIT_SUCCESS;
}
