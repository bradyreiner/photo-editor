#include "agent_edit_controller.h"

#include "app_bootstrap.h"
#include "catalog_asset_model.h"
#include "edit_recipe_state.h"
#include "export_job_controller.h"
#include "observability.h"

#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QTime>
#include <QTimer>

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
  QImage image(24, 16, QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.setPixelColor(x, y, QColor(92 + x, 105 + y, 118));
    }
  }
  const QFileInfo info(file_path);
  if (!QDir().mkpath(info.dir().absolutePath())) {
    return false;
  }
  return image.save(file_path, "PNG");
}

QJsonObject read_json_object(const QString& file_path) {
  QFile file(file_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QJsonDocument::fromJson(file.readAll()).object();
}

QByteArray read_file_bytes(const QString& file_path) {
  QFile file(file_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

QByteArray fake_ollama_response(const QJsonObject& content_object) {
  return QJsonDocument(QJsonObject{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"),
       QJsonObject{
           {QStringLiteral("role"), QStringLiteral("assistant")},
           {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(content_object).toJson(QJsonDocument::Compact))},
       }},
  }).toJson(QJsonDocument::Compact);
}

int observability_event_count(const QString& file_path, const QString& event_name) {
  QFile file(file_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return 0;
  }

  auto count = 0;
  while (!file.atEnd()) {
    const auto record = QJsonDocument::fromJson(file.readLine()).object();
    if (record.value(QStringLiteral("event")).toString() == event_name) {
      ++count;
    }
  }
  return count;
}

bool has_citation_field(const QVariantList& citations, const QString& field) {
  for (const auto& citation_value : citations) {
    const auto citation = citation_value.toMap();
    if (citation.value(QStringLiteral("field")).toString() == field) {
      return true;
    }
  }
  return false;
}

bool json_array_contains_string(const QJsonArray& values, const QString& expected) {
  for (const auto& value : values) {
    if (value.toString() == expected) {
      return true;
    }
  }
  return false;
}

QVariantMap model_by_id(const QVariantList& models, const QString& id) {
  for (const auto& model_value : models) {
    const auto model = model_value.toMap();
    if (model.value(QStringLiteral("id")).toString() == id) {
      return model;
    }
  }
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("AgentEditControllerTest"));

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("temporary runtime directory should be created");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  qputenv(
      "PHOTO_EDITOR_OBSERVABILITY_RUN_DIR",
      QDir(runtime_root.path()).filePath(QStringLiteral("observability")).toUtf8());
  photo_editor::desktop::observability::initialize();
  const auto observability_log_path =
      photo_editor::desktop::observability::desktopLogFilePath();

  const auto source_path = QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/source.png"));
  if (!write_fixture_image(source_path)) {
    return fail("fixture image should be written");
  }
  const auto second_source_path = QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/source-2.png"));
  if (!write_fixture_image(second_source_path)) {
    return fail("second fixture image should be written");
  }
  const auto third_source_path = QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/source-3.png"));
  if (!write_fixture_image(third_source_path)) {
    return fail("third fixture image should be written");
  }
  const auto original_source_bytes = read_file_bytes(source_path);
  const auto original_second_source_bytes = read_file_bytes(second_source_path);
  const auto original_third_source_bytes = read_file_bytes(third_source_path);
  if (original_source_bytes.isEmpty() ||
      original_second_source_bytes.isEmpty() ||
      original_third_source_bytes.isEmpty()) {
    return fail("fixture source image bytes should be readable before agent workflows");
  }

  photo_editor::desktop::EditRecipeState original_state;
  original_state.exposure = 0.0;
  original_state.contrast = 0.0;
  original_state.saturation = 0.0;
  original_state.output_color_space = QStringLiteral("Display P3");
  const auto original_recipe = photo_editor::desktop::serialize_edit_recipe_json(original_state);
  photo_editor::desktop::EditRecipeState second_original_state;
  second_original_state.exposure = 0.0;
  second_original_state.contrast = 0.0;
  second_original_state.saturation = 0.0;
  second_original_state.temperature = 12.0;
  const auto second_original_recipe =
      photo_editor::desktop::serialize_edit_recipe_json(second_original_state);
  photo_editor::desktop::EditRecipeState third_original_state;
  third_original_state.exposure = 0.0;
  third_original_state.contrast = 0.0;
  third_original_state.saturation = 0.0;
  third_original_state.temperature = 2.0;
  const auto third_original_recipe =
      photo_editor::desktop::serialize_edit_recipe_json(third_original_state);

  photo_editor::desktop::CatalogAssetModel catalog_model;
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.toggleSelectionIndex(0);

  photo_editor::desktop::AgentEditController controller;
  photo_editor::desktop::ExportJobController export_jobs;
  controller.setCatalogModel(&catalog_model);
  controller.setExportJobController(&export_jobs);

  const auto model_manifest_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("agent-models/models.json"));
  if (!QDir().mkpath(QFileInfo(model_manifest_path).dir().absolutePath())) {
    return fail("agent model manifest directory should be created");
  }
  QFile model_manifest_file(model_manifest_path);
  if (!model_manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return fail("agent model manifest should be writable");
  }
  model_manifest_file.write(QJsonDocument(QJsonObject{
      {QStringLiteral("models"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("id"), QStringLiteral("ollama-qwen-test")},
               {QStringLiteral("name"), QStringLiteral("Qwen test via Ollama")},
               {QStringLiteral("backend"), QStringLiteral("ollama")},
               {QStringLiteral("endpoint"), QStringLiteral("http://127.0.0.1:11434")},
               {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
               {QStringLiteral("vision"), true},
               {QStringLiteral("available"), true},
           },
       }},
  }).toJson(QJsonDocument::Compact));
  model_manifest_file.close();
  controller.reloadModels();
  controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));
  const auto ollama_setup = controller.prepareSelectedModelInstall();
  if (!ollama_setup.value(QStringLiteral("ok")).toBool() ||
      !ollama_setup.value(QStringLiteral("installable")).toBool() ||
      ollama_setup.value(QStringLiteral("command")).toString() != QStringLiteral("ollama pull qwen2.5vl:7b") ||
      ollama_setup.value(QStringLiteral("arguments")).toStringList() !=
          QStringList{QStringLiteral("pull"), QStringLiteral("qwen2.5vl:7b")}) {
    return fail("Ollama model setup should expose a bounded local pull command");
  }
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      QByteArrayLiteral(R"json({"model":"qwen2.5vl:7b","details":{"family":"qwen2","parameter_size":"7B","quantization_level":"Q4_K_M"}})json"));
  const auto controller_probe = controller.probeSelectedModel();
  const auto probed_ollama_model = model_by_id(controller.availableModels(), QStringLiteral("ollama-qwen-test"));
  if (!controller_probe.value(QStringLiteral("ok")).toBool() ||
      !controller_probe.value(QStringLiteral("available")).toBool() ||
      probed_ollama_model.value(QStringLiteral("status")).toString() != QStringLiteral("available") ||
      probed_ollama_model.value(QStringLiteral("modelFamily")).toString() != QStringLiteral("qwen2") ||
      probed_ollama_model.value(QStringLiteral("parameterSize")).toString() != QStringLiteral("7B") ||
      controller.modelInstallStatus() != QStringLiteral("Model available")) {
    return fail("controller model probe should update selected Ollama model runtime status");
  }
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  const auto unconfirmed_install = controller.installSelectedModel(false);
  if (unconfirmed_install.value(QStringLiteral("ok")).toBool() ||
      !unconfirmed_install.value(QStringLiteral("confirmationRequired")).toBool()) {
    return fail("Ollama install should require confirmation before starting a process");
  }
  const auto fake_ollama_path = QDir(runtime_root.path()).filePath(QStringLiteral("fake-ollama.sh"));
  QFile fake_ollama(fake_ollama_path);
  if (!fake_ollama.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return fail("fake Ollama executable should be writable");
  }
  fake_ollama.write(
      "#!/bin/sh\n"
      "printf '{\"status\":\"pulling manifest\"}\\n'\n"
      "printf '{\"status\":\"downloading\",\"digest\":\"sha256:test-layer\",\"total\":100,\"completed\":25}\\n'\n"
      "printf '{\"status\":\"downloading\",\"digest\":\"sha256:test-layer\",\"total\":100,\"completed\":100}\\n'\n"
      "printf '{\"status\":\"success\"}\\n'\n"
      "printf 'pulled %s %s\\n' \"$1\" \"$2\"\n"
      "exit 0\n");
  fake_ollama.close();
  QFile::setPermissions(
      fake_ollama_path,
      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
  qputenv("PHOTO_EDITOR_OLLAMA_BINARY", fake_ollama_path.toUtf8());
  const auto confirmed_install = controller.installSelectedModel(true);
  if (!confirmed_install.value(QStringLiteral("ok")).toBool() ||
      !confirmed_install.value(QStringLiteral("started")).toBool() ||
      !controller.modelInstallRunning()) {
    return fail("confirmed Ollama install should start the configured local process");
  }
  for (auto attempt = 0; attempt < 100 && controller.modelInstallRunning(); ++attempt) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(10);
  }
  if (controller.modelInstallRunning() ||
      controller.modelInstallStatus() != QStringLiteral("Installed") ||
      !controller.modelInstallOutput().contains(QStringLiteral("pulled pull qwen2.5vl:7b"))) {
    return fail("confirmed Ollama install should capture process completion and output");
  }
  const auto install_progress = controller.modelInstallProgress();
  if (install_progress.value(QStringLiteral("status")).toString() != QStringLiteral("success") ||
      install_progress.value(QStringLiteral("digest")).toString() != QStringLiteral("sha256:test-layer") ||
      install_progress.value(QStringLiteral("completedBytes")).toLongLong() != 100 ||
      install_progress.value(QStringLiteral("totalBytes")).toLongLong() != 100 ||
      install_progress.value(QStringLiteral("progressPercent")).toInt() != 100 ||
      install_progress.value(QStringLiteral("jsonLineCount")).toInt() < 4) {
    return fail("confirmed Ollama install should parse streaming pull progress JSON");
  }
  qunsetenv("PHOTO_EDITOR_OLLAMA_BINARY");
  controller.setSelectedModelId(QStringLiteral("rulebook-deterministic"));

  const auto invalid_style = controller.createStyle(
      QString(),
      QStringLiteral("Needs a name"),
      {},
      {},
      {},
      {});
  if (invalid_style.value(QStringLiteral("ok")).toBool()) {
    return fail("style creation should require a name and goal");
  }

  const QVariantMap allowed_tools{
      {QStringLiteral("raw"), true},
      {QStringLiteral("global_color"), true},
      {QStringLiteral("curves"), true},
      {QStringLiteral("hsl"), true},
      {QStringLiteral("color_grading"), false},
      {QStringLiteral("detail"), true},
      {QStringLiteral("crop"), QStringLiteral("conservative")},
      {QStringLiteral("masks"), true},
      {QStringLiteral("retouch"), false},
      {QStringLiteral("layers_luts"), false},
      {QStringLiteral("export"), false},
  };
  const auto created_style = controller.createStyle(
      QStringLiteral("Moody Test Style"),
      QStringLiteral("Moody editorial contrast with protected highlights and restrained saturation."),
      allowed_tools,
      QVariantMap{
          {QStringLiteral("avoid_clipped_highlights"), true},
          {QStringLiteral("avoid_crushed_blacks"), true},
          {QStringLiteral("max_exposure_shift"), 0.9},
          {QStringLiteral("max_temperature_shift"), 1200},
          {QStringLiteral("max_saturation_shift"), 10},
      },
      QVariantMap{
          {QStringLiteral("highlight_clip_limit_percent"), 0.4},
          {QStringLiteral("shadow_clip_limit_percent"), 0.8},
          {QStringLiteral("require_before_after_proof"), true},
      },
      QVariantMap{
          {QStringLiteral("cluster_by_lighting"), true},
          {QStringLiteral("maintain_sequence_consistency"), true},
          {QStringLiteral("correct_outliers_individually"), true},
      });
  if (!created_style.value(QStringLiteral("ok")).toBool()) {
    std::cerr << created_style.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString()
              << '\n';
    return fail("style creation should persist a valid user style");
  }
  if (controller.selectedStyleId() != QStringLiteral("moody-test-style")) {
    return fail("newly created style should become the selected agent style");
  }
  controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));
  qputenv("PHOTO_EDITOR_OLLAMA_BINARY", fake_ollama_path.toUtf8());
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      QByteArrayLiteral(R"json({"model":"qwen2.5vl:7b","details":{"family":"qwen2","parameter_size":"7B","quantization_level":"Q4_K_M"}})json"));
  const auto local_ai_setup = controller.setupLocalAi(false);
  qunsetenv("PHOTO_EDITOR_OLLAMA_BINARY");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  const auto setup_checks = local_ai_setup.value(QStringLiteral("checks")).toList();
  auto saw_model_response_check = false;
  auto saw_mcp_check = false;
  auto saw_proof_check = false;
  auto saw_model_proof_check = false;
  for (const auto& check_value : setup_checks) {
    const auto check = check_value.toMap();
    const auto key = check.value(QStringLiteral("key")).toString();
    if (key == QStringLiteral("model_can_respond") && check.value(QStringLiteral("ok")).toBool()) {
      saw_model_response_check = true;
    } else if (key == QStringLiteral("mcp_tools_reachable") &&
               check.value(QStringLiteral("ok")).toBool() &&
               !check.value(QStringLiteral("required")).toBool()) {
      saw_mcp_check = true;
    } else if (key == QStringLiteral("proof_rendering") &&
               check.value(QStringLiteral("ok")).toBool() &&
               !check.value(QStringLiteral("details")).toMap().value(QStringLiteral("skipped")).toBool()) {
      saw_proof_check = true;
    } else if (key == QStringLiteral("model_can_inspect_proof_image") &&
               check.value(QStringLiteral("ok")).toBool()) {
      saw_model_proof_check = true;
    }
  }
  const auto setup_proof_artifacts =
      local_ai_setup.value(QStringLiteral("proofReadiness")).toMap().value(QStringLiteral("artifacts")).toMap();
  if (!local_ai_setup.value(QStringLiteral("ok")).toBool() ||
      !local_ai_setup.value(QStringLiteral("setupComplete")).toBool() ||
      local_ai_setup.value(QStringLiteral("mode")).toString() != QStringLiteral("local-ai-setup") ||
      local_ai_setup.value(QStringLiteral("primaryAction")).toString() != QStringLiteral("ready") ||
      controller.status() != QStringLiteral("Local AI ready") ||
      !controller.errorMessage().isEmpty() ||
      controller.lastRunReport().value(QStringLiteral("mode")).toString() != QStringLiteral("local-ai-setup") ||
      local_ai_setup.value(QStringLiteral("setupMemoryPacket")).toMap().value(QStringLiteral("phase")).toString() !=
          QStringLiteral("setup") ||
      local_ai_setup.value(QStringLiteral("setupMemoryPacket")).toMap().value(QStringLiteral("rawVaultIncluded")).toBool() ||
      !saw_model_response_check ||
      !saw_mcp_check ||
      !saw_proof_check ||
      !saw_model_proof_check ||
      !QFileInfo(setup_proof_artifacts.value(QStringLiteral("beforeProofPath")).toString()).isFile() ||
      !QFileInfo(setup_proof_artifacts.value(QStringLiteral("afterProofPath")).toString()).isFile()) {
    return fail("one local AI setup action should probe model readiness, MCP status, setup memory, proof rendering, and publish visible state");
  }
  controller.setSelectedModelId(QStringLiteral("rulebook-deterministic"));
  const auto style_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("agent-styles/moody-test-style.json"));
  const auto persisted_style = read_json_object(style_path);
  if (persisted_style.value(QStringLiteral("name")).toString() != QStringLiteral("Moody Test Style") ||
      !persisted_style.value(QStringLiteral("allowed_tools")).toObject().value(QStringLiteral("masks")).toBool() ||
      persisted_style.value(QStringLiteral("constraints"))
              .toObject()
              .value(QStringLiteral("max_exposure_shift"))
              .toDouble() != 0.9) {
    return fail("created style should write user-controlled intent, permissions, and constraints");
  }
  const auto style_export_dir = QDir(runtime_root.path()).filePath(QStringLiteral("style-exports"));
  const auto exported_style = controller.exportStyle(QStringLiteral("moody-test-style"), style_export_dir);
  const auto exported_style_path = exported_style.value(QStringLiteral("exportPath")).toString();
  if (!exported_style.value(QStringLiteral("ok")).toBool() ||
      exported_style.value(QStringLiteral("mode")).toString() != QStringLiteral("style-export") ||
      exported_style.value(QStringLiteral("styleId")).toString() != QStringLiteral("moody-test-style") ||
      !QFileInfo(exported_style_path).isFile()) {
    return fail("style export should write a validated shareable style JSON file");
  }
  const auto imported_style = controller.importStyleJson(
      QString::fromUtf8(read_file_bytes(exported_style_path)),
      QStringLiteral("Imported Moody Test Style"));
  if (!imported_style.value(QStringLiteral("ok")).toBool() ||
      imported_style.value(QStringLiteral("mode")).toString() != QStringLiteral("style-import") ||
      imported_style.value(QStringLiteral("styleId")).toString() != QStringLiteral("moody-test-style-2") ||
      imported_style.value(QStringLiteral("styleName")).toString() !=
          QStringLiteral("Imported Moody Test Style") ||
      !imported_style.value(QStringLiteral("renamedOnImport")).toBool() ||
      controller.selectedStyleId() != QStringLiteral("moody-test-style-2") ||
      !QFileInfo(imported_style.value(QStringLiteral("importPath")).toString()).isFile()) {
    return fail("style import should validate JSON, avoid id collisions, reload styles, and select the import");
  }

  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
  });
  catalog_model.selectIndex(0);
  const auto edited_reference_id =
      catalog_model.createVirtualCopyForAsset(QStringLiteral("asset-1"), QStringLiteral("Edited reference"));
  if (edited_reference_id.isEmpty()) {
    return fail("test setup should create an edited reference virtual copy");
  }
  catalog_model.selectAll();
  controller.setSelectedStyleId(QStringLiteral("moody-test-style-2"));
  controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));
  const auto learnable_style_state = controller.aiEditWorkflowState();
  const auto learnable_style_action =
      learnable_style_state.value(QStringLiteral("primaryAction")).toMap();
  const auto learnable_style_candidate =
      learnable_style_state.value(QStringLiteral("styleLearning")).toMap();
  if (!learnable_style_state.value(QStringLiteral("ok")).toBool() ||
      learnable_style_state.value(QStringLiteral("statusLine")).toString() !=
          QStringLiteral("Needs style") ||
      learnable_style_action.value(QStringLiteral("key")).toString() !=
          QStringLiteral("learn_style") ||
      learnable_style_action.value(QStringLiteral("label")).toString() !=
          QStringLiteral("Learn This Style") ||
      !learnable_style_candidate.value(QStringLiteral("available")).toBool() ||
      learnable_style_candidate.value(QStringLiteral("editedReferenceCount")).toInt() != 1 ||
      learnable_style_candidate.value(QStringLiteral("usableRecipeCount")).toInt() != 2) {
    return fail("AI edit workflow should offer one Learn This Style action when edited references are selected");
  }
  const auto learned_style = controller.createLearnedStyleProfileFromSelection(
      QStringLiteral("Learned Wedding Style"),
      QStringLiteral("Match my warm clean wedding edits consistently."),
      QStringLiteral("Keep skin believable and protect dress highlights."));
  if (!learned_style.value(QStringLiteral("ok")).toBool() ||
      learned_style.value(QStringLiteral("mode")).toString() != QStringLiteral("learned-style-profile") ||
      learned_style.value(QStringLiteral("styleId")).toString() != QStringLiteral("learned-wedding-style") ||
      learned_style.value(QStringLiteral("sourceAssetCount")).toInt() != 2 ||
      learned_style.value(QStringLiteral("usableRecipeCount")).toInt() != 2 ||
      controller.selectedStyleId() != QStringLiteral("learned-wedding-style")) {
    std::cerr << learned_style.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString()
              << '\n';
    return fail("learned style creation should summarize selected edited assets and select the new style");
  }
  const auto learned_style_ai_state = controller.aiEditWorkflowState();
  const auto learned_taste_card =
      learned_style_ai_state.value(QStringLiteral("tasteCard")).toMap();
  if (learned_style_ai_state.value(QStringLiteral("statusLine")).toString() !=
          QStringLiteral("Ready") ||
      learned_style_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("key")).toString() !=
          QStringLiteral("create_samples") ||
      !learned_taste_card.value(QStringLiteral("available")).toBool() ||
      !learned_taste_card.value(QStringLiteral("learned")).toBool() ||
      learned_taste_card.value(QStringLiteral("referenceCount")).toInt() != 2 ||
      learned_taste_card.value(QStringLiteral("confidence")).toString().isEmpty() ||
      learned_taste_card.value(QStringLiteral("summary")).toString().isEmpty()) {
    return fail("AI edit workflow should show a compact learned-style taste card after learning from selected references");
  }
  const auto learned_style_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("agent-styles/learned-wedding-style.json"));
  const auto persisted_learned_style = read_json_object(learned_style_path);
  const auto learned_profile =
      persisted_learned_style.value(QStringLiteral("learned_profile")).toObject();
  const auto learned_persistence_ok =
      persisted_learned_style.value(QStringLiteral("reference_policy"))
              .toObject()
              .value(QStringLiteral("mode"))
              .toString() == QStringLiteral("learned_style_profile") &&
      learned_profile.value(QStringLiteral("source_asset_count")).toInt() == 2 &&
      learned_profile.value(QStringLiteral("reference_examples")).toArray().size() == 2 &&
      learned_profile.value(QStringLiteral("before_after_pair_count")).toInt() == 1 &&
      learned_profile.value(QStringLiteral("before_after_pairs")).toArray().size() == 1 &&
      learned_profile.value(QStringLiteral("visualFingerprintSummary")).toObject().value(QStringLiteral("referenceFingerprintCount")).toInt() >= 1 &&
      !persisted_learned_style.value(QStringLiteral("tool_knowledge")).toObject().isEmpty() &&
      !persisted_learned_style.value(QStringLiteral("quality_model")).toObject().isEmpty() &&
      !persisted_learned_style.value(QStringLiteral("project_workflow")).toObject().isEmpty();
  if (!learned_persistence_ok) {
    return fail("learned style should persist reference metadata, tool knowledge, quality model, and project workflow");
  }
  const auto style_memory_path = learned_style.value(QStringLiteral("memoryNotePath")).toString();
  const auto style_memory_note = QString::fromUtf8(read_file_bytes(style_memory_path));
  const auto tool_memory_path = learned_style.value(QStringLiteral("toolMemoryNotePath")).toString();
  const auto tool_memory_note = QString::fromUtf8(read_file_bytes(tool_memory_path));
  const auto learned_memory_events =
      learned_style.value(QStringLiteral("userVisibleMemoryEvents")).toList();
  const auto learned_memory_event =
      learned_memory_events.isEmpty() ? QVariantMap{} : learned_memory_events.first().toMap();
  if (!QFileInfo(style_memory_path).isFile() ||
      !style_memory_note.contains(QStringLiteral("type: agent_style_memory")) ||
      !style_memory_note.contains(QStringLiteral("schema_version: 1")) ||
      !style_memory_note.contains(QStringLiteral("# Learned Wedding Style")) ||
      !style_memory_note.contains(QStringLiteral("## Reference Examples")) ||
      !style_memory_note.contains(QStringLiteral("## Related Memory")) ||
      !style_memory_note.contains(QStringLiteral("backlinks:")) ||
      !style_memory_note.contains(QStringLiteral("asset-1")) ||
      style_memory_note.contains(runtime_root.path()) ||
      style_memory_note.contains(QStringLiteral("data:image")) ||
      style_memory_note.contains(QStringLiteral("base64"))) {
    return fail("learned style creation should write an inspectable local Markdown memory note without image bytes or local source paths");
  }
  if (learned_memory_event.value(QStringLiteral("type")).toString() !=
          QStringLiteral("durable-style-memory") ||
      learned_memory_event.value(QStringLiteral("changeType")).toString() !=
          QStringLiteral("style_profile_created") ||
      !learned_memory_event.value(QStringLiteral("requiresUserVisibility")).toBool() ||
      learned_memory_event.value(QStringLiteral("notePaths"))
              .toMap()
              .value(QStringLiteral("styleMemoryNotePath"))
              .toString() != style_memory_path) {
    return fail("learned style creation should emit a user-visible durable memory event");
  }
  if (!QFileInfo(tool_memory_path).isFile() ||
      !tool_memory_note.contains(QStringLiteral("type: agent_tool_memory")) ||
      !tool_memory_note.contains(QStringLiteral("schema_version: 1")) ||
      !tool_memory_note.contains(QStringLiteral("links:\n  - \"[[learned-wedding-style]]\"")) ||
      !tool_memory_note.contains(QStringLiteral("# Tool Rulebook - Learned Wedding Style")) ||
      !tool_memory_note.contains(QStringLiteral("globalTone")) ||
      !tool_memory_note.contains(QStringLiteral("Protect highlight detail")) ||
      tool_memory_note.contains(runtime_root.path()) ||
      tool_memory_note.contains(QStringLiteral("data:image")) ||
      tool_memory_note.contains(QStringLiteral("base64"))) {
    return fail("learned style creation should write an inspectable tool rulebook memory note without image bytes or local paths");
  }
  const auto initial_vault_status = controller.memoryVaultStatus();
  const auto initial_note_counts = initial_vault_status.value(QStringLiteral("noteCounts")).toMap();
  const auto initial_memory_root_path =
      initial_vault_status.value(QStringLiteral("rootPath")).toString();
  if (!initial_vault_status.value(QStringLiteral("ok")).toBool() ||
      !initial_vault_status.value(QStringLiteral("obsidianCompatible")).toBool() ||
      !initial_vault_status.value(QStringLiteral("localFirst")).toBool() ||
      initial_vault_status.value(QStringLiteral("storesImageBytes")).toBool() ||
      initial_vault_status.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
      !initial_vault_status.value(QStringLiteral("schemaVersionOk")).toBool() ||
      initial_vault_status.value(QStringLiteral("schemaMismatchCount")).toInt() != 0 ||
      initial_vault_status.value(QStringLiteral("migrationRequired")).toBool() ||
      initial_vault_status.value(QStringLiteral("contextStrategy")).toString() !=
          QStringLiteral("retrieved-packets-not-raw-vault") ||
      initial_vault_status.value(QStringLiteral("vaultFingerprint")).toString().isEmpty() ||
      initial_vault_status.value(QStringLiteral("externalChangeDetected")).toBool() ||
      initial_vault_status.value(QStringLiteral("externalChangeCount")).toInt() != 0 ||
      initial_note_counts.value(QStringLiteral("styles")).toInt() != 1 ||
      initial_note_counts.value(QStringLiteral("tools")).toInt() != 1 ||
      initial_vault_status.value(QStringLiteral("activeStyleNotePath")).toString() !=
          style_memory_path) {
    return fail("memory vault status should expose an Obsidian-compatible local Markdown vault without image payloads");
  }
  const auto setup_memory_packet = controller.memoryPacketForPhase(QStringLiteral("setup"));
  if (!setup_memory_packet.value(QStringLiteral("ok")).toBool() ||
      setup_memory_packet.value(QStringLiteral("phase")).toString() != QStringLiteral("setup") ||
      !setup_memory_packet.value(QStringLiteral("includedMemoryKinds")).toStringList().isEmpty() ||
      setup_memory_packet.value(QStringLiteral("memoryBudget"))
              .toMap()
              .value(QStringLiteral("maxCards"))
              .toInt() != 0 ||
      setup_memory_packet.value(QStringLiteral("memoryBudget"))
          .toMap()
          .value(QStringLiteral("rawVaultIncluded"))
          .toBool()) {
    return fail("setup memory retrieval should avoid style memory and raw vault context");
  }
  const auto sample_memory_packet = controller.memoryPacketForPhase(QStringLiteral("sample"));
  if (!sample_memory_packet.value(QStringLiteral("ok")).toBool() ||
      sample_memory_packet.value(QStringLiteral("phase")).toString() != QStringLiteral("sample") ||
      sample_memory_packet.value(QStringLiteral("rawVaultIncluded")).toBool() ||
      !sample_memory_packet.value(QStringLiteral("includedMemoryKinds")).toStringList().contains(QStringLiteral("style")) ||
      sample_memory_packet.value(QStringLiteral("retrievalPlan")).toList().isEmpty() ||
      !sample_memory_packet.value(QStringLiteral("relevancePolicy"))
           .toMap()
           .value(QStringLiteral("deterministicFirstPass"))
           .toBool() ||
      sample_memory_packet.value(QStringLiteral("relevancePolicy"))
              .toMap()
              .value(QStringLiteral("secondarySimilaritySearch"))
              .toMap()
              .value(QStringLiteral("role"))
              .toString() != QStringLiteral("secondary-ranking-only") ||
      sample_memory_packet.value(QStringLiteral("styleMemoryNotePath")).toString() != style_memory_path ||
      sample_memory_packet.value(QStringLiteral("styleCard"))
              .toMap()
              .value(QStringLiteral("styleName"))
              .toString() != QStringLiteral("Learned Wedding Style") ||
      sample_memory_packet.value(QStringLiteral("toolMemoryNotePath")).toString() != tool_memory_path ||
      sample_memory_packet.value(QStringLiteral("toolCard")).toMap().value(QStringLiteral("tools")).toList().isEmpty()) {
    return fail("agent memory retrieval should return compact style/tool packets instead of the full memory vault");
  }
  const auto project_plan = controller.planStyleProjectRun(QStringLiteral("selected-assets"));
  if (!project_plan.value(QStringLiteral("ok")).toBool() ||
      project_plan.value(QStringLiteral("mode")).toString() != QStringLiteral("style-project-plan") ||
      project_plan.value(QStringLiteral("assetCount")).toInt() != 2 ||
      project_plan.value(QStringLiteral("clusterCount")).toInt() < 1 ||
      project_plan.value(QStringLiteral("sampleAssetIds")).toStringList().isEmpty() ||
      project_plan.value(QStringLiteral("beforeAfterPairCount")).toInt() != 1 ||
      project_plan.value(QStringLiteral("styleMatchScoring")).toString().isEmpty()) {
    return fail("style project planning should expose clusters, sample assets, before/after pairs, and style-match policy");
  }
  const auto project_memory_path = project_plan.value(QStringLiteral("projectMemoryNotePath")).toString();
  const auto project_memory_note = QString::fromUtf8(read_file_bytes(project_memory_path));
  const auto project_style_memory_note = QString::fromUtf8(read_file_bytes(style_memory_path));
  if (!QFileInfo(project_memory_path).isFile() ||
      !project_memory_note.contains(QStringLiteral("type: agent_project_memory")) ||
      !project_memory_note.contains(QStringLiteral("schema_version: 1")) ||
      !project_memory_note.contains(QStringLiteral("links:\n  - \"[[learned-wedding-style]]\"")) ||
      !project_memory_note.contains(QStringLiteral("Related style: [[learned-wedding-style]]")) ||
      !project_memory_note.contains(QStringLiteral("## Sample Assets")) ||
      !project_memory_note.contains(QStringLiteral("asset-1")) ||
      !project_style_memory_note.contains(QStringLiteral("[[learned-wedding-style-selected-assets]]")) ||
      project_memory_note.contains(runtime_root.path()) ||
      project_memory_note.contains(QStringLiteral("data:image")) ||
      project_memory_note.contains(QStringLiteral("base64"))) {
    return fail("project planning should write a local Obsidian-style project memory note without source paths or image bytes");
  }
  const auto feedback_report = controller.recordStyleFeedback(
      QStringLiteral("Samples should be a little warmer but keep dress highlights clean."),
      QVariantMap{{QStringLiteral("sampleApproved"), false}});
  if (!feedback_report.value(QStringLiteral("ok")).toBool() ||
      feedback_report.value(QStringLiteral("mode")).toString() != QStringLiteral("style-feedback") ||
      feedback_report.value(QStringLiteral("feedbackEventCount")).toInt() != 1) {
    return fail("style feedback should update the active learned profile");
  }
  const auto feedback_style = read_json_object(learned_style_path);
  if (feedback_style.value(QStringLiteral("learned_profile"))
          .toObject()
          .value(QStringLiteral("feedback_event_count"))
          .toInt() != 1 ||
      feedback_style.value(QStringLiteral("learned_profile"))
          .toObject()
          .value(QStringLiteral("latestFeedback"))
          .toString()
          .isEmpty()) {
    return fail("style feedback should persist into the learned profile file");
  }
  const auto updated_style_memory_note = QString::fromUtf8(read_file_bytes(style_memory_path));
  const auto feedback_memory_path =
      feedback_report.value(QStringLiteral("feedbackMemoryNotePath")).toString();
  const auto feedback_memory_note = QString::fromUtf8(read_file_bytes(feedback_memory_path));
  const auto feedback_note_link =
      QStringLiteral("[[%1]]").arg(QFileInfo(feedback_memory_path).completeBaseName());
  const auto feedback_memory_events =
      feedback_report.value(QStringLiteral("userVisibleMemoryEvents")).toList();
  const auto feedback_memory_event =
      feedback_memory_events.isEmpty() ? QVariantMap{} : feedback_memory_events.first().toMap();
  if (!updated_style_memory_note.contains(QStringLiteral("## Feedback History")) ||
      !updated_style_memory_note.contains(QStringLiteral("Samples should be a little warmer")) ||
      !QFileInfo(feedback_memory_path).isFile() ||
      !feedback_memory_note.contains(QStringLiteral("type: agent_feedback_memory")) ||
      !feedback_memory_note.contains(QStringLiteral("schema_version: 1")) ||
      !feedback_memory_note.contains(QStringLiteral("links:\n  - \"[[learned-wedding-style]]\"")) ||
      !feedback_memory_note.contains(QStringLiteral("Samples should be a little warmer")) ||
      !updated_style_memory_note.contains(feedback_note_link) ||
      feedback_memory_note.contains(runtime_root.path()) ||
      feedback_memory_note.contains(QStringLiteral("data:image")) ||
      feedback_memory_note.contains(QStringLiteral("base64"))) {
    return fail("style feedback should write inspectable feedback memory and refresh the style memory note");
  }
  const auto feedback_run_history = controller.runHistory();
  const auto latest_history =
      feedback_run_history.isEmpty() ? QVariantMap{} : feedback_run_history.first().toMap();
  const auto latest_history_memory_events =
      latest_history.value(QStringLiteral("userVisibleMemoryEvents")).toList();
  if (feedback_memory_event.value(QStringLiteral("type")).toString() !=
          QStringLiteral("durable-style-memory") ||
      feedback_memory_event.value(QStringLiteral("changeType")).toString() !=
          QStringLiteral("style_feedback_saved") ||
      !feedback_memory_event.value(QStringLiteral("requiresUserVisibility")).toBool() ||
      feedback_report.value(QStringLiteral("styleChangeSummary")).toString() !=
          QStringLiteral("Style changed because: Samples should be a little warmer but keep dress highlights clean.") ||
      feedback_memory_event.value(QStringLiteral("styleChangeSummary")).toString() !=
          feedback_report.value(QStringLiteral("styleChangeSummary")).toString() ||
      feedback_memory_event.value(QStringLiteral("detail")).toString() !=
          feedback_report.value(QStringLiteral("styleChangeSummary")).toString() ||
      !feedback_memory_event.value(QStringLiteral("memoryWriteApproved")).toBool() ||
      feedback_memory_event.value(QStringLiteral("notePaths"))
              .toMap()
              .value(QStringLiteral("feedbackMemoryNotePath"))
              .toString() != feedback_memory_path ||
      latest_history_memory_events.isEmpty()) {
    return fail("style feedback should emit user-visible durable memory events in reports and run history");
  }
  const auto feedback_vault_status = controller.memoryVaultStatus();
  const auto feedback_note_counts = feedback_vault_status.value(QStringLiteral("noteCounts")).toMap();
  if (feedback_note_counts.value(QStringLiteral("styles")).toInt() != 1 ||
      feedback_note_counts.value(QStringLiteral("projects")).toInt() != 1 ||
      feedback_note_counts.value(QStringLiteral("tools")).toInt() != 1 ||
      feedback_note_counts.value(QStringLiteral("feedback")).toInt() != 1 ||
      !feedback_vault_status.value(QStringLiteral("schemaVersionOk")).toBool() ||
      feedback_vault_status.value(QStringLiteral("migrationRequired")).toBool() ||
      feedback_vault_status.value(QStringLiteral("externalChangeDetected")).toBool() ||
      feedback_vault_status.value(QStringLiteral("totalNoteCount")).toInt() < 4) {
    return fail("memory vault status should count style, project, tool, and feedback notes after the agent learns from corrections");
  }
  QFile external_feedback_edit(feedback_memory_path);
  if (!external_feedback_edit.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text) ||
      external_feedback_edit.write("\nExternal Obsidian note: keep ceremony skin tones consistent.\n") <= 0) {
    return fail("test should be able to simulate an external Obsidian memory edit");
  }
  external_feedback_edit.close();
  const auto external_memory_status = controller.memoryVaultStatus();
  const auto external_changed_files =
      external_memory_status.value(QStringLiteral("externalChangedFiles")).toList();
  auto saw_feedback_memory_change = false;
  for (const auto& changed_file_value : external_changed_files) {
    const auto changed_file = changed_file_value.toMap();
    if (changed_file.value(QStringLiteral("relativePath")).toString() ==
            QDir(initial_memory_root_path).relativeFilePath(feedback_memory_path) &&
        changed_file.value(QStringLiteral("changeType")).toString() == QStringLiteral("modified")) {
      saw_feedback_memory_change = true;
    }
  }
  if (!external_memory_status.value(QStringLiteral("externalChangeDetected")).toBool() ||
      external_memory_status.value(QStringLiteral("externalChangeCount")).toInt() < 1 ||
      !saw_feedback_memory_change ||
      external_memory_status.value(QStringLiteral("externalChangePolicy")).toString() !=
          QStringLiteral("warn-user-before-overwriting-obsidian-edits")) {
    return fail("memory vault status should detect Markdown notes changed outside the app while it is running");
  }
  const auto obsidian_open_dry_run = controller.openMemoryVaultInObsidian(true);
  if (!obsidian_open_dry_run.value(QStringLiteral("ok")).toBool() ||
      obsidian_open_dry_run.value(QStringLiteral("mode")).toString() !=
          QStringLiteral("agent-memory-vault-open-obsidian") ||
      !obsidian_open_dry_run.value(QStringLiteral("dryRun")).toBool() ||
      !obsidian_open_dry_run.value(QStringLiteral("deepLinkReady")).toBool() ||
      obsidian_open_dry_run.value(QStringLiteral("fallback")).toString() !=
          QStringLiteral("folder-open") ||
      !obsidian_open_dry_run.value(QStringLiteral("obsidianUrl"))
           .toString()
           .startsWith(QStringLiteral("obsidian://open?path=")) ||
      obsidian_open_dry_run.value(QStringLiteral("rootPath")).toString() != initial_memory_root_path) {
    return fail("memory vault should expose a dry-run Obsidian deep link with folder-open fallback metadata");
  }
  const auto memory_export = controller.exportMemoryVault(
      QDir(runtime_root.path()).filePath(QStringLiteral("memory-exports")),
      true);
  const auto memory_export_path = memory_export.value(QStringLiteral("exportPath")).toString();
  const auto memory_export_manifest_path =
      memory_export.value(QStringLiteral("manifestPath")).toString();
  const auto memory_export_manifest =
      QJsonDocument::fromJson(read_file_bytes(memory_export_manifest_path)).object();
  const auto exported_style_note_path =
      QDir(memory_export_path).filePath(QStringLiteral("styles/learned-wedding-style.md"));
  const auto exported_style_note = QString::fromUtf8(read_file_bytes(exported_style_note_path));
  if (!memory_export.value(QStringLiteral("ok")).toBool() ||
      !memory_export.value(QStringLiteral("redacted")).toBool() ||
      memory_export.value(QStringLiteral("noteCount")).toInt() < 4 ||
      !QFileInfo(memory_export_manifest_path).isFile() ||
      !memory_export_manifest.value(QStringLiteral("redacted")).toBool() ||
      memory_export_manifest.value(QStringLiteral("memorySchemaVersion")).toInt() != 1 ||
      memory_export_manifest.value(QStringLiteral("sourceVaultIncluded")).toBool() ||
      !QFileInfo(exported_style_note_path).isFile() ||
      !exported_style_note.contains(QStringLiteral("schema_version: 1")) ||
      exported_style_note.contains(runtime_root.path()) ||
      exported_style_note.contains(QStringLiteral("data:image")) ||
      exported_style_note.contains(QStringLiteral("base64"))) {
    return fail("memory vault export should create a redacted inspectable Markdown copy with a manifest");
  }
  const auto unconfirmed_memory_reset = controller.resetMemoryVault(false);
  if (unconfirmed_memory_reset.value(QStringLiteral("ok")).toBool() ||
      !unconfirmed_memory_reset.value(QStringLiteral("requiresConfirmation")).toBool() ||
      !QFileInfo(style_memory_path).isFile()) {
    return fail("memory vault reset should require explicit confirmation and leave notes untouched when unconfirmed");
  }
  const auto revision_memory_packet = controller.memoryPacketForPhase(QStringLiteral("revision"));
  if (!revision_memory_packet.value(QStringLiteral("ok")).toBool() ||
      revision_memory_packet.value(QStringLiteral("phase")).toString() != QStringLiteral("revision") ||
      revision_memory_packet.value(QStringLiteral("styleCard"))
              .toMap()
              .value(QStringLiteral("latestFeedback"))
              .toString()
              .isEmpty()) {
    return fail("revision memory packet should include latest approved feedback in compact form");
  }
  const auto learned_validation = controller.validateBatchForScope(QStringLiteral("selected-assets"));
  const auto learned_validation_reports = learned_validation.value(QStringLiteral("assetReports")).toList();
  const auto first_learned_style_match =
      learned_validation_reports.isEmpty()
          ? QVariantMap{}
          : learned_validation_reports.first()
                .toMap()
                .value(QStringLiteral("validationReport"))
                .toMap()
                .value(QStringLiteral("styleMatch"))
                .toMap();
  if (!learned_validation.value(QStringLiteral("ok")).toBool() ||
      !first_learned_style_match.value(QStringLiteral("available")).toBool() ||
      first_learned_style_match.value(QStringLiteral("method")).toString() !=
          QStringLiteral("metadata_recipe_visual_fingerprint_v1") ||
      first_learned_style_match.value(QStringLiteral("scorePercent")).toInt() <= 0) {
    return fail("learned-style validation should include per-image style-match scoring");
  }
  catalog_model.clearVirtualCopiesForTesting();
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  controller.setSelectedStyleId(QStringLiteral("moody-test-style-2"));
  controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));

  const auto ready_ai_state = controller.aiEditWorkflowState();
  if (!ready_ai_state.value(QStringLiteral("ok")).toBool() ||
      ready_ai_state.value(QStringLiteral("mode")).toString() != QStringLiteral("ai-edit-workflow-state") ||
      ready_ai_state.value(QStringLiteral("statusLine")).toString() != QStringLiteral("Ready") ||
      ready_ai_state.value(QStringLiteral("scope")).toString() != QStringLiteral("current-photo") ||
      ready_ai_state.value(QStringLiteral("assetCount")).toInt() != 1 ||
      ready_ai_state.value(QStringLiteral("scopeLabel")).toString() != QStringLiteral("1 photo") ||
      ready_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("key")).toString() !=
          QStringLiteral("create_samples") ||
      ready_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("label")).toString() !=
          QStringLiteral("Apply Edit") ||
      ready_ai_state.value(QStringLiteral("secondaryActions")).toList().size() < 5) {
    return fail("AI edit workflow state should expose one ready status, one direct apply primary action, and compact secondary actions");
  }
  catalog_model.toggleSelectionIndex(1);
  const auto multi_ai_state = controller.aiEditWorkflowState();
  if (multi_ai_state.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      multi_ai_state.value(QStringLiteral("assetCount")).toInt() != 2 ||
      multi_ai_state.value(QStringLiteral("scopeLabel")).toString() != QStringLiteral("2 selected photos")) {
    return fail("AI edit workflow state should infer selected-set scope for multiple selected photos");
  }
  catalog_model.clearSelection();
  catalog_model.createProject(QStringLiteral("Workflow Wedding"));
  catalog_model.addAssetToProject(QStringLiteral("asset-1"), QStringLiteral("Workflow Wedding"));
  catalog_model.addAssetToProject(QStringLiteral("asset-2"), QStringLiteral("Workflow Wedding"));
  catalog_model.openProject(QStringLiteral("Workflow Wedding"));
  const auto project_ai_state = controller.aiEditWorkflowState();
  if (project_ai_state.value(QStringLiteral("scope")).toString() != QStringLiteral("project") ||
      project_ai_state.value(QStringLiteral("assetCount")).toInt() != 2 ||
      project_ai_state.value(QStringLiteral("scopeLabel")).toString() != QStringLiteral("2 project photos")) {
    return fail("AI edit workflow state should infer project scope when no assets are selected and a project is active");
  }
  catalog_model.setProjectFilter(QStringLiteral("all"));
  catalog_model.setSearchFilter(QStringLiteral("source-2"));
  const auto filtered_ai_state = controller.aiEditWorkflowState();
  if (filtered_ai_state.value(QStringLiteral("scope")).toString() != QStringLiteral("filtered-library") ||
      filtered_ai_state.value(QStringLiteral("assetCount")).toInt() != 1) {
    return fail("AI edit workflow state should infer filtered-library scope when no assets are selected and a filter is active");
  }
  catalog_model.setSearchFilter(QString());
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  controller.setSelectedStyleId(QStringLiteral("missing-style"));
  const auto missing_style_state = controller.aiEditWorkflowState();
  if (missing_style_state.value(QStringLiteral("ok")).toBool() ||
      missing_style_state.value(QStringLiteral("statusLine")).toString() != QStringLiteral("Needs style") ||
      missing_style_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("key")).toString() !=
          QStringLiteral("choose_style") ||
      missing_style_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("enabled")).toBool()) {
    return fail("AI edit workflow state should reduce missing style setup to a single Choose Style action");
  }
  controller.setSelectedStyleId(QStringLiteral("moody-test-style-2"));
  controller.setSelectedModelId(QStringLiteral("missing-model"));
  const auto missing_model_state = controller.aiEditWorkflowState();
  if (missing_model_state.value(QStringLiteral("ok")).toBool() ||
      !missing_model_state.value(QStringLiteral("setupRequired")).toBool() ||
      missing_model_state.value(QStringLiteral("statusLine")).toString() != QStringLiteral("Set up local AI") ||
      missing_model_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("key")).toString() !=
          QStringLiteral("set_up_local_ai") ||
      !missing_model_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("enabled")).toBool()) {
    return fail("AI edit workflow state should reduce local model readiness to one setup action");
  }
  controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));

  const QJsonObject needs_input_content{
      {QStringLiteral("decision"), QStringLiteral("needs_user_input")},
      {QStringLiteral("summary"), QStringLiteral("Need the target reception warmth before editing.")},
      {QStringLiteral("userQuestions"),
       QJsonArray{QStringLiteral("Should the reception photos stay warm editorial or neutral documentary?")}},
  };
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON");
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON", fake_ollama_response(needs_input_content));
  controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));
  const auto needs_input_report =
      controller.runDraftOnSelected(QStringLiteral("edit this wedding sample to my style"));
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  if (needs_input_report.value(QStringLiteral("ok")).toBool() ||
      needs_input_report.value(QStringLiteral("decision")).toString() != QStringLiteral("needs_user_input") ||
      !needs_input_report.value(QStringLiteral("needsUserInput")).toBool() ||
      !controller.awaitingUserInput() ||
      controller.pendingUserQuestions().isEmpty() ||
      controller.status() != QStringLiteral("Needs user input") ||
      controller.draftActive()) {
    return fail("model clarification should become pending controller input without applying a draft");
  }
  if (controller.pendingUserQuestions().first().toString() !=
      QStringLiteral("Should the reception photos stay warm editorial or neutral documentary?")) {
    return fail("pending model clarification should preserve the user-facing question");
  }
  const auto pending_ai_state = controller.aiEditWorkflowState();
  if (pending_ai_state.value(QStringLiteral("statusLine")).toString() !=
          QStringLiteral("Waiting for your answer") ||
      !pending_ai_state.value(QStringLiteral("reviewRequired")).toBool() ||
      !pending_ai_state.value(QStringLiteral("awaitingUserInput")).toBool() ||
      pending_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("key")).toString() !=
          QStringLiteral("answer_question")) {
    return fail("AI edit workflow state should make model clarification a review state");
  }

  const QJsonObject answered_patch_content{
      {QStringLiteral("decision"), QStringLiteral("recipe_patch")},
      {QStringLiteral("summary"), QStringLiteral("Warm reception edit with protected highlights.")},
      {QStringLiteral("styleConstraintCitations"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("field"), QStringLiteral("style.goal")},
               {QStringLiteral("reason"), QStringLiteral("The user answered that the reception should stay warm.")},
           },
       }},
      {QStringLiteral("recipePatch"),
       QJsonObject{
           {QStringLiteral("globalTone"),
            QJsonObject{
                {QStringLiteral("exposure"), 0.10},
                {QStringLiteral("highlights"), -12.0},
            }},
           {QStringLiteral("color"),
            QJsonObject{
                {QStringLiteral("temperature"), 4.0},
            }},
       }},
  };
  const auto answer_request_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("ollama-clarification-answer-request.json"));
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON", fake_ollama_response(answered_patch_content));
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH", answer_request_path.toUtf8());
  const auto answered_report =
      controller.answerPendingUserInput(QStringLiteral("Keep reception warm but protect skin and dress highlights."));
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH");
  const auto answer_request_bytes = read_file_bytes(answer_request_path);
  if (!answered_report.value(QStringLiteral("ok")).toBool() ||
      controller.awaitingUserInput() ||
      !controller.pendingUserQuestions().isEmpty() ||
      !controller.draftActive() ||
      !answer_request_bytes.contains("User answer to agent question") ||
      !answer_request_bytes.contains("Keep reception warm but protect skin and dress highlights")) {
    return fail("answering a pending model clarification should resume planning with the answer in context");
  }
  const auto draft_ai_state = controller.aiEditWorkflowState();
  if (draft_ai_state.value(QStringLiteral("statusLine")).toString() != QStringLiteral("Ready to commit") ||
      !draft_ai_state.value(QStringLiteral("draftActive")).toBool() ||
      !draft_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("confirmationRequired")).toBool() ||
      draft_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("key")).toString() !=
          QStringLiteral("commit")) {
    return fail("AI edit workflow state should surface a single commit action for active drafts");
  }
  const auto answered_rollback = controller.rollbackDraft();
  if (!answered_rollback.value(QStringLiteral("ok")).toBool() || controller.draftActive()) {
    return fail("draft created after a model clarification answer should remain rollbackable");
  }

  const QJsonObject high_risk_patch_content{
      {QStringLiteral("decision"), QStringLiteral("recipe_patch")},
      {QStringLiteral("summary"), QStringLiteral("Tighten the composition with a strong editorial crop.")},
      {QStringLiteral("styleConstraintCitations"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("field"), QStringLiteral("style.rules.crop")},
               {QStringLiteral("reason"), QStringLiteral("The style allows composition cleanup with review.")},
           },
       }},
      {QStringLiteral("recipePatch"),
       QJsonObject{
           {QStringLiteral("crop"),
            QJsonObject{
                {QStringLiteral("cropX"), 0.12},
                {QStringLiteral("cropY"), 0.08},
                {QStringLiteral("cropWidth"), 0.82},
                {QStringLiteral("cropHeight"), 0.82},
                {QStringLiteral("cropZoom"), 1.18},
            }},
       }},
  };
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON", fake_ollama_response(high_risk_patch_content));
  const auto high_risk_stop =
      controller.runDraftOnSelected(QStringLiteral("make the sample more editorial"));
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  const auto high_risk_policy =
      high_risk_stop.value(QStringLiteral("preDraftStopPolicy")).toMap();
  const auto high_risk_assessment =
      high_risk_stop.value(QStringLiteral("riskAssessment")).toMap();
  if (high_risk_stop.value(QStringLiteral("ok")).toBool() ||
      high_risk_stop.value(QStringLiteral("decision")).toString() != QStringLiteral("needs_user_input") ||
      high_risk_stop.value(QStringLiteral("safeStopReason")).toString() !=
          QStringLiteral("high_risk_or_low_confidence") ||
      !high_risk_stop.value(QStringLiteral("needsUserInput")).toBool() ||
      !high_risk_policy.value(QStringLiteral("stopRequired")).toBool() ||
      !high_risk_policy.value(QStringLiteral("triggers")).toStringList().contains(QStringLiteral("high_risk")) ||
      !high_risk_assessment.value(QStringLiteral("approvalTypes")).toStringList().contains(QStringLiteral("large_crop")) ||
      !controller.awaitingUserInput() ||
      controller.pendingUserQuestions().isEmpty() ||
      controller.status() != QStringLiteral("Needs user input") ||
      controller.draftActive()) {
    return fail("model-planned high-risk edits should stop for user approval before creating a draft");
  }
  const auto high_risk_dismiss = controller.dismissPendingAgentInput();
  if (!high_risk_dismiss.value(QStringLiteral("ok")).toBool() || controller.awaitingUserInput()) {
    return fail("dismissing a high-risk pre-draft stop should clear pending input");
  }
  controller.setSelectedModelId(QStringLiteral("rulebook-deterministic"));

  controller.setUserInstructions(QStringLiteral("bright warm editorial draft with soft highlights"));

  const auto recipe_before_manual_gate = catalog_model.selectedRecipeJson();
  controller.setManualEditingActive(true);
  const auto paused_agent_run =
      controller.runDraftOnSelected(QStringLiteral("should wait for manual edits"));
  if (paused_agent_run.value(QStringLiteral("ok")).toBool() ||
      paused_agent_run.value(QStringLiteral("backgroundWorkPolicy"))
              .toMap()
              .value(QStringLiteral("action"))
              .toString() != QStringLiteral("deferred") ||
      !paused_agent_run.value(QStringLiteral("backgroundWorkPolicy"))
           .toMap()
           .value(QStringLiteral("manualEditingActive"))
           .toBool() ||
      controller.draftActive() ||
      catalog_model.selectedRecipeJson() != recipe_before_manual_gate) {
    return fail("agent draft work should pause without mutating recipes during active manual editing");
  }
  controller.setManualEditingActive(false);

  const auto recipe_before_analysis = catalog_model.selectedRecipeJson();
  const auto analysis = controller.analyzeSelected();
  if (!analysis.value(QStringLiteral("ok")).toBool()) {
    std::cerr << analysis.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString()
              << '\n';
    return fail("agent analyze-only run should succeed");
  }
  if (analysis.value(QStringLiteral("mode")).toString() != QStringLiteral("analyze")) {
    return fail("analyze-only report should identify its run mode");
  }
  if (controller.draftActive()) {
    return fail("analyze-only run should not create an active draft");
  }
  if (catalog_model.selectedRecipeJson() != recipe_before_analysis) {
    return fail("analyze-only run should not mutate the selected recipe");
  }
  if (analysis.value(QStringLiteral("assetMetadata")).toMap().value(QStringLiteral("cameraLabel")).toString() !=
      QStringLiteral("Test Camera")) {
    return fail("analyze-only report should include selected asset metadata");
  }
  if (analysis.value(QStringLiteral("recipeSummary")).toMap().value(QStringLiteral("snapshotCount")).toInt() != 0) {
    return fail("analyze-only report should inspect the current recipe before edits");
  }
  if (analysis.value(QStringLiteral("editGoals")).toList().isEmpty() ||
      !analysis.value(QStringLiteral("riskAssessment")).toMap().contains(QStringLiteral("riskCount"))) {
    return fail("analyze-only report should name edit goals and current-image risks");
  }
  if (!QFileInfo(analysis.value(QStringLiteral("currentProofPath")).toString()).isFile() ||
      !QFileInfo(analysis.value(QStringLiteral("analysisReportPath")).toString()).isFile() ||
      !QFileInfo(analysis.value(QStringLiteral("manifestPath")).toString()).isFile()) {
    return fail("analyze-only run should persist current proof, report, and manifest artifacts");
  }
  if (!analysis.value(QStringLiteral("validationReport")).toMap().value(QStringLiteral("ok")).toBool()) {
    return fail("analyze-only report should validate the rendered current image");
  }
  const auto analysis_proof_source =
      analysis.value(QStringLiteral("proofRenderSource")).toMap();
  if (analysis_proof_source.value(QStringLiteral("sourceResolutionPolicy")).toString() !=
          QStringLiteral("bounded-preview") ||
      analysis_proof_source.value(QStringLiteral("sourceDecodeMaxEdge")).toInt() <= 0 ||
      !analysis_proof_source.value(QStringLiteral("usesDecodedImageCache")).toBool() ||
      analysis_proof_source.value(QStringLiteral("fullResolutionRequired")).toBool()) {
    return fail("analyze-only report should use a bounded cached preview source for proof rendering");
  }
  if (analysis.value(QStringLiteral("durationMs")).toInt() < 0 ||
      analysis.value(QStringLiteral("startedAtUtc")).toString().isEmpty() ||
      analysis.value(QStringLiteral("finishedAtUtc")).toString().isEmpty() ||
      analysis.value(QStringLiteral("toolCalls")).toList().isEmpty() ||
      analysis.value(QStringLiteral("performanceBudget"))
              .toMap()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("within-budget") ||
      !analysis.value(QStringLiteral("performanceProofGate"))
           .toMap()
           .value(QStringLiteral("passed"))
           .toBool()) {
    return fail("analyze-only report should include timing, performance budget, proof gate, and tool call metadata");
  }
  auto run_history = controller.runHistory();
  if (run_history.isEmpty() ||
      run_history.first().toMap().value(QStringLiteral("mode")).toString() != QStringLiteral("analyze") ||
      run_history.first().toMap().value(QStringLiteral("manifestPath")).toString().isEmpty() ||
      !run_history.first().toMap().contains(QStringLiteral("durationMs")) ||
      run_history.first().toMap().value(QStringLiteral("durationMs")).toInt() < 0 ||
      run_history.first()
              .toMap()
              .value(QStringLiteral("performanceBudget"))
              .toMap()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("within-budget")) {
    return fail("agent controller should expose analyze runs in user-facing run history");
  }
  const auto persisted_analysis = read_json_object(analysis.value(QStringLiteral("analysisReportPath")).toString());
  if (persisted_analysis.value(QStringLiteral("mode")).toString() != QStringLiteral("analyze")) {
    return fail("analysis artifact should persist the read-only report");
  }
  const auto analysis_manifest = read_json_object(analysis.value(QStringLiteral("manifestPath")).toString());
  if (analysis_manifest.value(QStringLiteral("style"))
              .toObject()
              .value(QStringLiteral("schemaVersion"))
              .toInt() != 1 ||
      analysis_manifest.value(QStringLiteral("model"))
              .toObject()
              .value(QStringLiteral("backend"))
              .toString() != QStringLiteral("local")) {
    return fail("analysis manifest should persist style schema and model backend metadata");
  }
  if (analysis_manifest.value(QStringLiteral("toolCalls")).toArray().isEmpty() ||
      analysis_manifest.value(QStringLiteral("durationMs")).toInt(-1) < 0 ||
      analysis_manifest.value(QStringLiteral("performanceBudget"))
              .toObject()
              .value(QStringLiteral("budgetMs"))
              .toInt() <= 0 ||
      !analysis_manifest.value(QStringLiteral("performanceProofGate"))
           .toObject()
           .value(QStringLiteral("passed"))
           .toBool()) {
    return fail("analysis manifest should persist timing, performance proof gate, and tool call metadata");
  }

  const auto export_dir = QDir(runtime_root.path()).filePath(QStringLiteral("exports"));
  if (!QDir().mkpath(export_dir)) {
    return fail("export destination should be created for agent preflight testing");
  }
  const auto recipe_before_preflight = catalog_model.selectedRecipeJson();
  const auto preflight = controller.exportPreflightForSelected(
      QStringLiteral("Web Proof"),
      export_dir,
      QVariantMap{
          {QStringLiteral("histogramSampleCount"), 100},
          {QStringLiteral("shadowClipCount"), 0},
          {QStringLiteral("highlightClipCount"), 2},
      });
  if (!preflight.value(QStringLiteral("ok")).toBool() ||
      preflight.value(QStringLiteral("mode")).toString() != QStringLiteral("export-preflight") ||
      preflight.value(QStringLiteral("checkedCount")).toInt() != 1 ||
      preflight.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1 ||
      preflight.value(QStringLiteral("warningCount")).toInt() < 2 ||
      preflight.value(QStringLiteral("assetReports")).toList().size() != 1 ||
      !QFileInfo(preflight.value(QStringLiteral("preflightReportPath")).toString()).isFile() ||
      !QFileInfo(preflight.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.draftActive()) {
    return fail("agent export preflight should report and persist selected asset export readiness without creating a draft");
  }
  const auto persisted_preflight =
      read_json_object(preflight.value(QStringLiteral("preflightReportPath")).toString());
  if (persisted_preflight.value(QStringLiteral("mode")).toString() != QStringLiteral("export-preflight") ||
      persisted_preflight.value(QStringLiteral("assetReports")).toArray().size() != 1 ||
      persisted_preflight.value(QStringLiteral("toolCalls")).toArray().isEmpty() ||
      persisted_preflight.value(QStringLiteral("durationMs")).toInt(-1) < 0 ||
      !persisted_preflight.value(QStringLiteral("performanceBudget"))
           .toObject()
           .value(QStringLiteral("withinBudget"))
           .toBool()) {
    return fail("agent export preflight artifact should persist the report and performance budget");
  }
  const auto preflight_asset_report = preflight.value(QStringLiteral("assetReports")).toList().first().toMap();
  const auto preflight_parity =
      preflight_asset_report.value(QStringLiteral("previewExportParity")).toMap();
  if (preflight_asset_report.value(QStringLiteral("assetId")).toString() != QStringLiteral("asset-1") ||
      preflight_asset_report.value(QStringLiteral("preset")).toString() != QStringLiteral("Web Proof") ||
      preflight_asset_report.value(QStringLiteral("destinationDirectory")).toString() != export_dir ||
      preflight_asset_report.value(QStringLiteral("status")).toString() != QStringLiteral("ready") ||
      !preflight_parity.value(QStringLiteral("required")).toBool() ||
      preflight_parity.value(QStringLiteral("status")).toString() != QStringLiteral("required-at-export") ||
      !preflight_parity.value(QStringLiteral("highRiskGroups"))
           .toStringList()
           .contains(QStringLiteral("output-profile-proofing"))) {
    return fail("agent export preflight should include per-asset preset, destination, status, and parity proof requirements");
  }
  if (catalog_model.selectedRecipeJson() != recipe_before_preflight) {
    return fail("agent export preflight should not mutate the selected recipe");
  }

  const auto unconfirmed_export = controller.queueExportForSelected(
      QStringLiteral("Web Proof"),
      export_dir,
      false,
      QVariantMap{{QStringLiteral("histogramSampleCount"), 100}});
  if (unconfirmed_export.value(QStringLiteral("ok")).toBool() ||
      unconfirmed_export.value(QStringLiteral("queuedCount")).toInt() != 0 ||
      export_jobs.jobCount() != 0) {
    return fail("agent export should require explicit confirmation before queueing jobs");
  }
  const auto confirmed_export = controller.queueExportForSelected(
      QStringLiteral("Web Proof"),
      export_dir,
      true,
      QVariantMap{
          {QStringLiteral("histogramSampleCount"), 100},
          {QStringLiteral("highlightClipCount"), 2},
      });
  if (!confirmed_export.value(QStringLiteral("ok")).toBool() ||
      confirmed_export.value(QStringLiteral("mode")).toString() != QStringLiteral("export-batch") ||
      confirmed_export.value(QStringLiteral("queuedCount")).toInt() != 1 ||
      confirmed_export.value(QStringLiteral("expectedCount")).toInt() != 1 ||
      confirmed_export.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1 ||
      export_jobs.jobCount() != 1 ||
      !QFileInfo(confirmed_export.value(QStringLiteral("exportBatchReportPath")).toString()).isFile() ||
      !QFileInfo(confirmed_export.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.draftActive()) {
    return fail("confirmed agent export should queue and persist the selected asset through the export controller");
  }
  const auto persisted_export_batch =
      read_json_object(confirmed_export.value(QStringLiteral("exportBatchReportPath")).toString());
  if (persisted_export_batch.value(QStringLiteral("mode")).toString() != QStringLiteral("export-batch") ||
      persisted_export_batch.value(QStringLiteral("toolCalls")).toArray().size() < 2 ||
      persisted_export_batch.value(QStringLiteral("durationMs")).toInt(-1) < 0) {
    return fail("confirmed agent export artifact should persist timing and tool call outcomes");
  }

  const auto revised_draft = controller.applyDraftRecipePatchToSelected(
      QVariantMap{
          {QStringLiteral("globalTone"),
           QVariantMap{
               {QStringLiteral("exposure"), 5.0},
               {QStringLiteral("highlights"), 100.0},
               {QStringLiteral("whites"), 100.0},
           }},
      });
  const auto revised_asset_reports = revised_draft.value(QStringLiteral("assetReports")).toList();
  const auto revised_asset_report =
      revised_asset_reports.isEmpty() ? QVariantMap{} : revised_asset_reports.first().toMap();
  const auto revised_edit_loop = revised_asset_report.value(QStringLiteral("editLoop")).toMap();
  if (!revised_draft.value(QStringLiteral("ok")).toBool() ||
      revised_draft.value(QStringLiteral("editLoopVersion")).toString() != QStringLiteral("agent-edit-loop-v1") ||
      revised_draft.value(QStringLiteral("editLoops")).toList().isEmpty() ||
      revised_edit_loop.value(QStringLiteral("version")).toString() != QStringLiteral("agent-edit-loop-v1") ||
      revised_edit_loop.value(QStringLiteral("attemptCount")).toInt() < 2 ||
      revised_edit_loop.value(QStringLiteral("maxAttempts")).toInt() != 3 ||
      revised_edit_loop.value(QStringLiteral("finalScore")).toDouble() <= 0.0 ||
      revised_edit_loop.value(QStringLiteral("attempts")).toList().first().toMap().value(QStringLiteral("patchHash")).toString().isEmpty() ||
      revised_asset_report.value(QStringLiteral("revisionCount")).toInt() < 1 ||
      revised_asset_report.value(QStringLiteral("qualityScore")).toDouble() <= 0.0 ||
      revised_asset_report.value(QStringLiteral("validationIterations")).toList().size() < 2 ||
      revised_draft.value(QStringLiteral("requestedRecipePatch")).toMap() ==
          revised_draft.value(QStringLiteral("recipePatch")).toMap() ||
      !controller.draftActive()) {
    return fail("agent draft should run a scored edit-verify loop and revise unsafe patches before applying");
  }
  const auto live_run_events = controller.runEvents();
  auto saw_render_event = false;
  auto saw_validation_event = false;
  auto saw_review_ready_event = false;
  for (const auto& event_value : live_run_events) {
    const auto event = event_value.toMap();
    const auto stage = event.value(QStringLiteral("stage")).toString();
    const auto status = event.value(QStringLiteral("status")).toString();
    saw_render_event = saw_render_event || stage == QStringLiteral("render");
    saw_validation_event = saw_validation_event || stage == QStringLiteral("validate");
    saw_review_ready_event = saw_review_ready_event ||
                             (stage == QStringLiteral("review") && status == QStringLiteral("ready"));
  }
  if (!saw_render_event || !saw_validation_event || !saw_review_ready_event ||
      controller.lastRunReport().value(QStringLiteral("runEvents")).toList().isEmpty()) {
    return fail("agent drafts should publish live render, validation, and review-ready events into the final report");
  }
  const auto revised_rollback = controller.rollbackDraft();
  if (!revised_rollback.value(QStringLiteral("ok")).toBool() || controller.draftActive()) {
    return fail("revised validation-feedback draft should remain rollbackable");
  }

  const QJsonObject unsafe_plan_content{
      {QStringLiteral("decision"), QStringLiteral("recipe_patch")},
      {QStringLiteral("summary"), QStringLiteral("Unsafe first pass for repair coverage")},
      {QStringLiteral("styleConstraintCitations"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("field"), QStringLiteral("allowedTools.color")},
               {QStringLiteral("reason"), QStringLiteral("Color edits are allowed.")},
           },
       }},
      {QStringLiteral("recipePatch"),
       QJsonObject{
           {QStringLiteral("globalTone"),
            QJsonObject{
                {QStringLiteral("exposure"), 5.0},
                {QStringLiteral("highlights"), 100.0},
                {QStringLiteral("whites"), 100.0},
            }},
       }},
  };
  const QJsonObject repaired_plan_content{
      {QStringLiteral("decision"), QStringLiteral("recipe_patch")},
      {QStringLiteral("summary"), QStringLiteral("Reduced highlight risk after validation")},
      {QStringLiteral("styleConstraintCitations"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("field"), QStringLiteral("validation.highlight_clip_limit_percent")},
               {QStringLiteral("reason"), QStringLiteral("Repair lowers highlight pressure.")},
           },
       }},
      {QStringLiteral("recipePatch"),
       QJsonObject{
           {QStringLiteral("globalTone"),
            QJsonObject{
                {QStringLiteral("exposure"), 0.10},
                {QStringLiteral("highlights"), -12.0},
                {QStringLiteral("whites"), -8.0},
            }},
       }},
  };
  const QJsonObject proof_critique_content{
      {QStringLiteral("pass"), false},
      {QStringLiteral("summary"), QStringLiteral("Rendered proof is too bright and needs highlight repair.")},
      {QStringLiteral("issues"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("kind"), QStringLiteral("highlight_clipping")},
               {QStringLiteral("severity"), QStringLiteral("high")},
           },
       }},
      {QStringLiteral("repairHints"),
       QJsonArray{
           QStringLiteral("Lower exposure and pull highlights before accepting the draft."),
       }},
      {QStringLiteral("needsUserReview"), true},
      {QStringLiteral("tasteConfidence"), QStringLiteral("medium")},
  };
  const QJsonObject repaired_proof_critique_content{
      {QStringLiteral("pass"), true},
      {QStringLiteral("summary"), QStringLiteral("Repaired proof is balanced enough to accept.")},
      {QStringLiteral("issues"), QJsonArray{}},
      {QStringLiteral("repairHints"), QJsonArray{}},
      {QStringLiteral("needsUserReview"), false},
      {QStringLiteral("tasteConfidence"), QStringLiteral("high")},
  };
  const QJsonArray repair_sequence{
      QJsonObject{
          {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
          {QStringLiteral("done"), true},
          {QStringLiteral("message"),
           QJsonObject{
               {QStringLiteral("role"), QStringLiteral("assistant")},
               {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(unsafe_plan_content).toJson(QJsonDocument::Compact))},
           }},
      },
      QJsonObject{
          {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
          {QStringLiteral("done"), true},
          {QStringLiteral("message"),
           QJsonObject{
               {QStringLiteral("role"), QStringLiteral("assistant")},
               {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(proof_critique_content).toJson(QJsonDocument::Compact))},
           }},
      },
      QJsonObject{
          {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
          {QStringLiteral("done"), true},
          {QStringLiteral("message"),
           QJsonObject{
               {QStringLiteral("role"), QStringLiteral("assistant")},
               {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(repaired_plan_content).toJson(QJsonDocument::Compact))},
           }},
      },
      QJsonObject{
          {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
          {QStringLiteral("done"), true},
          {QStringLiteral("message"),
           QJsonObject{
               {QStringLiteral("role"), QStringLiteral("assistant")},
               {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(repaired_proof_critique_content).toJson(QJsonDocument::Compact))},
           }},
      },
  };
  const auto repair_request_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("ollama-repair-requests.jsonl"));
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON",
      QJsonDocument(repair_sequence).toJson(QJsonDocument::Compact));
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH", repair_request_path.toUtf8());
  controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));
  const auto model_repaired_draft =
      controller.runDraftOnSelected(QStringLiteral("make a bright edit but obey validation feedback"));
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH");
  const auto model_repaired_asset_reports =
      model_repaired_draft.value(QStringLiteral("assetReports")).toList();
  const auto model_repaired_asset_report =
      model_repaired_asset_reports.isEmpty() ? QVariantMap{} : model_repaired_asset_reports.first().toMap();
  const auto model_repaired_repair_budget =
      model_repaired_asset_report.value(QStringLiteral("repairBudget")).toMap();
  auto saw_model_repair_tool_call = false;
  auto saw_model_proof_critique_tool_call = false;
  for (const auto& tool_call_value : model_repaired_draft.value(QStringLiteral("toolCalls")).toList()) {
    const auto name = tool_call_value.toMap().value(QStringLiteral("name")).toString();
    if (name == QStringLiteral("model_evaluate_rendered_proof")) {
      saw_model_proof_critique_tool_call = true;
    }
    if (name == QStringLiteral("model_repair_recipe_patch_from_validation") ||
        name == QStringLiteral("apply_model_repaired_recipe_patch")) {
      saw_model_repair_tool_call = true;
    }
  }
  const auto model_repaired_loop_attempts =
      model_repaired_asset_report.value(QStringLiteral("editLoop")).toMap().value(QStringLiteral("attempts")).toList();
  const auto model_repaired_first_attempt =
      model_repaired_loop_attempts.isEmpty() ? QVariantMap{} : model_repaired_loop_attempts.first().toMap();
  const auto model_repaired_second_attempt =
      model_repaired_loop_attempts.size() < 2 ? QVariantMap{} : model_repaired_loop_attempts.at(1).toMap();
  const auto model_repaired_critique =
      model_repaired_first_attempt.value(QStringLiteral("modelProofCritique")).toMap();
  const auto model_repaired_accepting_critique =
      model_repaired_second_attempt.value(QStringLiteral("modelProofCritique")).toMap();
  const auto repair_request_bytes = read_file_bytes(repair_request_path);
  if (!model_repaired_draft.value(QStringLiteral("ok")).toBool() ||
      model_repaired_asset_report.value(QStringLiteral("modelRepairCount")).toInt() < 1 ||
      model_repaired_asset_report.value(QStringLiteral("revisionCount")).toInt() < 1 ||
      !model_repaired_repair_budget.value(QStringLiteral("hardLimit")).toBool() ||
      model_repaired_repair_budget.value(QStringLiteral("maxValidationRevisionIterations")).toInt() != 2 ||
      model_repaired_repair_budget.value(QStringLiteral("maxModelRepairTurns")).toInt() != 2 ||
      model_repaired_asset_report.value(QStringLiteral("editLoop")).toMap().value(QStringLiteral("attemptCount")).toInt() < 2 ||
      model_repaired_asset_report.value(QStringLiteral("modelRepairReports")).toList().isEmpty() ||
      model_repaired_draft.value(QStringLiteral("requestedRecipePatch")).toMap() ==
          model_repaired_draft.value(QStringLiteral("recipePatch")).toMap() ||
      !saw_model_proof_critique_tool_call ||
      !saw_model_repair_tool_call ||
      !model_repaired_critique.value(QStringLiteral("ok")).toBool() ||
      model_repaired_critique.value(QStringLiteral("pass")).toBool() ||
      model_repaired_critique.value(QStringLiteral("repairHints")).toList().isEmpty() ||
      !model_repaired_accepting_critique.value(QStringLiteral("ok")).toBool() ||
      !model_repaired_accepting_critique.value(QStringLiteral("pass")).toBool() ||
      !repair_request_bytes.contains("Validation warnings JSON") ||
      !repair_request_bytes.contains("current-proof-validation") ||
      !repair_request_bytes.contains("imageStats") ||
      !repair_request_bytes.contains("vectorscope") ||
      !repair_request_bytes.contains("You are a local photo-editing proof evaluator") ||
      !repair_request_bytes.contains("Previous attempts JSON") ||
      !controller.draftActive()) {
    return fail("model-planned drafts should use a scored model repair turn with validation history before procedural fallback");
  }
  const auto model_repair_rollback = controller.rollbackDraft();
  if (!model_repair_rollback.value(QStringLiteral("ok")).toBool() || controller.draftActive()) {
    return fail("model-repaired validation draft should remain rollbackable");
  }
  controller.setSelectedModelId(QStringLiteral("rulebook-deterministic"));

  const auto representative_proof = controller.applyDraftRecipePatchToSelected(
      QVariantMap{
          {QStringLiteral("globalTone"),
           QVariantMap{
               {QStringLiteral("exposure"), 0.20},
               {QStringLiteral("contrast"), 4.0},
           }},
          {QStringLiteral("crop"),
           QVariantMap{
               {QStringLiteral("cropX"), 0.10},
               {QStringLiteral("cropY"), 0.10},
               {QStringLiteral("cropWidth"), 0.80},
               {QStringLiteral("cropHeight"), 0.80},
               {QStringLiteral("cropZoom"), 1.02},
           }},
          {QStringLiteral("masks"),
           QVariantMap{
               {QStringLiteral("maskStack"),
                QVariantList{
                    QVariantMap{
                        {QStringLiteral("id"), QStringLiteral("representative-brush")},
                        {QStringLiteral("kind"), QStringLiteral("brush")},
                        {QStringLiteral("name"), QStringLiteral("Representative Brush")},
                        {QStringLiteral("operation"), QStringLiteral("add")},
                        {QStringLiteral("exposure"), 0.35},
                        {QStringLiteral("feather"), 0.08},
                        {QStringLiteral("brushPoints"),
                         QVariantList{
                             QVariantMap{
                                 {QStringLiteral("x"), 0.50},
                                 {QStringLiteral("y"), 0.48},
                                 {QStringLiteral("size"), 0.12},
                                 {QStringLiteral("flow"), 0.75},
                             },
                         }},
                    },
                }},
           }},
      });
  const auto representative_validation =
      representative_proof.value(QStringLiteral("validationReport")).toMap();
  const auto representative_artifacts =
      representative_validation.value(QStringLiteral("artifacts")).toMap();
  const auto representative_diff = representative_proof.value(QStringLiteral("recipeDiff")).toMap();
  const auto representative_mask_local_proof =
      representative_proof.value(QStringLiteral("maskLocalProof")).toMap();
  const auto representative_batch_summary =
      representative_proof.value(QStringLiteral("batchSummary")).toMap();
  if (!representative_proof.value(QStringLiteral("ok")).toBool() ||
      !QFileInfo(representative_artifacts.value(QStringLiteral("beforeProofPath")).toString()).isFile() ||
      !QFileInfo(representative_artifacts.value(QStringLiteral("afterProofPath")).toString()).isFile() ||
      representative_validation.value(QStringLiteral("proofWidth")).toInt() <= 0 ||
      representative_validation.value(QStringLiteral("proofHeight")).toInt() <= 0 ||
      !representative_diff.contains(QStringLiteral("globalTone")) ||
      !representative_diff.contains(QStringLiteral("crop")) ||
      !representative_diff.contains(QStringLiteral("masks")) ||
      !representative_mask_local_proof.value(QStringLiteral("required")).toBool() ||
      !representative_mask_local_proof.value(QStringLiteral("proofAvailable")).toBool() ||
      representative_mask_local_proof.value(QStringLiteral("uiProofSurface")).toString() !=
          QStringLiteral("agent-dialog-before-after-proof") ||
      representative_mask_local_proof.value(QStringLiteral("maskStackCount")).toInt() != 1 ||
      representative_mask_local_proof.value(QStringLiteral("localAdjustmentFieldCount")).toInt() <= 0 ||
      representative_batch_summary.value(QStringLiteral("maskLocalProofReadyCount")).toInt() != 1 ||
      !controller.draftActive()) {
    return fail("representative color, crop, and mask draft should render proof artifacts and grouped diffs");
  }
  const auto representative_risk =
      representative_proof.value(QStringLiteral("riskAssessment")).toMap();
  const auto representative_approval_types =
      representative_risk.value(QStringLiteral("approvalTypes")).toStringList();
  const auto representative_manifest =
      read_json_object(representative_proof.value(QStringLiteral("manifestPath")).toString());
  const auto representative_manifest_risk =
      representative_manifest.value(QStringLiteral("riskAssessment")).toObject();
  if (!representative_risk.value(QStringLiteral("approvalRequired")).toBool() ||
      !representative_approval_types.contains(QStringLiteral("large_crop")) ||
      representative_risk.value(QStringLiteral("approvalRequests")).toList().isEmpty() ||
      !json_array_contains_string(
          representative_manifest_risk.value(QStringLiteral("approvalTypes")).toArray(),
          QStringLiteral("large_crop"))) {
    return fail("large crop drafts should carry explicit approval requirements in reports and manifests");
  }
  const QImage representative_after(
      representative_artifacts.value(QStringLiteral("afterProofPath")).toString());
  if (representative_after.isNull()) {
    return fail("representative draft after-proof image should be readable");
  }
  const auto representative_rollback = controller.rollbackDraft();
  if (!representative_rollback.value(QStringLiteral("ok")).toBool() || controller.draftActive()) {
    return fail("representative proof draft should remain rollbackable");
  }

  auto retouch_allowed_tools = allowed_tools;
  retouch_allowed_tools.insert(QStringLiteral("retouch"), true);
  const auto retouch_style = controller.createStyle(
      QStringLiteral("Retouch Approval Style"),
      QStringLiteral("Retouch testing style with explicit cleanup approval."),
      retouch_allowed_tools,
      {},
      {},
      {});
  if (!retouch_style.value(QStringLiteral("ok")).toBool()) {
    return fail("retouch approval test should create a style with retouch permission");
  }
  const QVariantMap retouch_patch{
      {QStringLiteral("retouch"),
       QVariantMap{
           {QStringLiteral("retouchStack"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("agent-heal-approval")},
                    {QStringLiteral("kind"), QStringLiteral("brush")},
                    {QStringLiteral("name"), QStringLiteral("Agent Heal Approval")},
                    {QStringLiteral("retouchMode"), QStringLiteral("heal")},
                    {QStringLiteral("removeStrength"), 68.0},
                    {QStringLiteral("healTextureBlend"), 0.32},
                    {QStringLiteral("healColorBlend"), 0.74},
                    {QStringLiteral("brushSize"), 0.06},
                    {QStringLiteral("brushFlow"), 0.78},
                    {QStringLiteral("committed"), false},
                    {QStringLiteral("brushPoints"),
                     QVariantList{
                         QVariantMap{
                             {QStringLiteral("x"), 0.36},
                             {QStringLiteral("y"), 0.44},
                             {QStringLiteral("size"), 0.06},
                             {QStringLiteral("flow"), 0.78},
                         },
                     }},
                },
            }},
       }},
  };
  const auto retouch_draft = controller.applyDraftRecipePatchToSelected(retouch_patch);
  const auto retouch_risk = retouch_draft.value(QStringLiteral("riskAssessment")).toMap();
  const auto retouch_approval_types =
      retouch_risk.value(QStringLiteral("approvalTypes")).toStringList();
  const auto retouch_manifest =
      read_json_object(retouch_draft.value(QStringLiteral("manifestPath")).toString());
  const auto retouch_manifest_risk =
      retouch_manifest.value(QStringLiteral("riskAssessment")).toObject();
  if (!retouch_draft.value(QStringLiteral("ok")).toBool() ||
      !retouch_risk.value(QStringLiteral("approvalRequired")).toBool() ||
      !retouch_approval_types.contains(QStringLiteral("retouch")) ||
      !json_array_contains_string(
          retouch_manifest_risk.value(QStringLiteral("approvalTypes")).toArray(),
          QStringLiteral("retouch")) ||
      !controller.draftActive()) {
    return fail("retouch drafts should carry explicit approval requirements in reports and manifests");
  }
  const auto retouch_rollback = controller.rollbackDraft();
  if (!retouch_rollback.value(QStringLiteral("ok")).toBool() || controller.draftActive()) {
    return fail("retouch approval draft should remain rollbackable");
  }

  catalog_model.createProject(QStringLiteral("Wedding Scope Approval"));
  catalog_model.addAssetToProject(QStringLiteral("asset-1"), QStringLiteral("Wedding Scope Approval"));
  catalog_model.openProject(QStringLiteral("Wedding Scope Approval"));
  const auto outside_project_scope_draft =
      controller.applyDraftRecipePatchForScope(
          QStringLiteral("filtered-library"),
          QVariantMap{
              {QStringLiteral("globalTone"),
               QVariantMap{
                   {QStringLiteral("exposure"), 0.05},
               }},
          });
  const auto outside_project_scope_risk =
      outside_project_scope_draft.value(QStringLiteral("riskAssessment")).toMap();
  const auto outside_project_scope_approval_types =
      outside_project_scope_risk.value(QStringLiteral("approvalTypes")).toStringList();
  const auto outside_project_scope_manifest =
      read_json_object(outside_project_scope_draft.value(QStringLiteral("manifestPath")).toString());
  const auto outside_project_scope_manifest_risk =
      outside_project_scope_manifest.value(QStringLiteral("riskAssessment")).toObject();
  if (!outside_project_scope_draft.value(QStringLiteral("ok")).toBool() ||
      outside_project_scope_draft.value(QStringLiteral("scope")).toString() !=
          QStringLiteral("filtered-library") ||
      !outside_project_scope_risk.value(QStringLiteral("approvalRequired")).toBool() ||
      !outside_project_scope_approval_types.contains(QStringLiteral("outside_project_scope")) ||
      !outside_project_scope_risk.value(QStringLiteral("scopeContext"))
           .toMap()
           .value(QStringLiteral("scopeEscapesActiveProject"))
           .toBool() ||
      !json_array_contains_string(
          outside_project_scope_manifest_risk.value(QStringLiteral("approvalTypes")).toArray(),
          QStringLiteral("outside_project_scope"))) {
    return fail("draft operations outside the active project scope should require explicit approval in reports and manifests");
  }
  const auto outside_project_scope_rollback = controller.rollbackDraft();
  if (!outside_project_scope_rollback.value(QStringLiteral("ok")).toBool() || controller.draftActive()) {
    return fail("outside-project-scope approval draft should remain rollbackable");
  }
  catalog_model.setProjectFilter(QStringLiteral("all"));
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  controller.setSelectedStyleId(QStringLiteral("moody-test-style-2"));

  const auto report = controller.runDraftOnSelected(QString());
  if (!report.value(QStringLiteral("ok")).toBool()) {
    std::cerr << report.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString()
              << '\n';
    return fail("agent draft should succeed");
  }
  if (!controller.draftActive()) {
    return fail("successful agent run should leave a draft active");
  }
  if (report.value(QStringLiteral("runId")).toString().isEmpty()) {
    return fail("draft report should include a run id");
  }
  const auto last_draft_report = controller.lastRunReport();
  const auto run_memory_path = last_draft_report.value(QStringLiteral("runMemoryNotePath")).toString();
  const auto run_memory_note = QString::fromUtf8(read_file_bytes(run_memory_path));
  const auto issue_memory_path = last_draft_report.value(QStringLiteral("issueMemoryNotePath")).toString();
  const auto issue_memory_note = QString::fromUtf8(read_file_bytes(issue_memory_path));
  if (last_draft_report.value(QStringLiteral("runId")).toString() !=
          report.value(QStringLiteral("runId")).toString() ||
      !QFileInfo(run_memory_path).isFile() ||
      !run_memory_note.contains(QStringLiteral("type: agent_run_memory")) ||
      !run_memory_note.contains(QStringLiteral("schema_version: 1")) ||
      !run_memory_note.contains(QStringLiteral("links:\n  - \"[[%1]]\"")
                                    .arg(report.value(QStringLiteral("styleId")).toString())) ||
      !run_memory_note.contains(report.value(QStringLiteral("runId")).toString()) ||
      !run_memory_note.contains(QStringLiteral("Related style: [[%1]]")
                                    .arg(report.value(QStringLiteral("styleId")).toString())) ||
      !run_memory_note.contains(QStringLiteral("## Safe Proof Links")) ||
      !run_memory_note.contains(QStringLiteral("Generated proofs are linked as relative app-owned artifacts")) ||
      !run_memory_note.contains(QStringLiteral("agent-runs/")) ||
      !run_memory_note.contains(QStringLiteral("../agent-runs/")) ||
      !run_memory_note.contains(QStringLiteral("after.png")) ||
      !run_memory_note.contains(QStringLiteral("safeArtifactReferences")) ||
      !run_memory_note.contains(QStringLiteral("mcp.tools/list")) ||
      run_memory_note.contains(runtime_root.path()) ||
      run_memory_note.contains(QStringLiteral("data:image")) ||
      run_memory_note.contains(QStringLiteral("base64"))) {
    return fail("successful drafts should persist a compact local run memory note without source paths or image bytes");
  }
  if (!QFileInfo(issue_memory_path).isFile() ||
      !issue_memory_note.contains(QStringLiteral("type: agent_issue_memory")) ||
      !issue_memory_note.contains(QStringLiteral("schema_version: 1")) ||
      !issue_memory_note.contains(report.value(QStringLiteral("runId")).toString()) ||
      !issue_memory_note.contains(QStringLiteral("[[%1]]")
                                      .arg(report.value(QStringLiteral("runId")).toString())) ||
      !issue_memory_note.contains(QStringLiteral("## Repair Strategy")) ||
      !issue_memory_note.contains(QStringLiteral("render_proof_and_validate")) ||
      issue_memory_note.contains(runtime_root.path()) ||
      issue_memory_note.contains(QStringLiteral("data:image")) ||
      issue_memory_note.contains(QStringLiteral("base64"))) {
    return fail("runs with validation warnings should persist issue memory with repair strategy and no source paths or image bytes");
  }
  const auto draft_style_memory_path =
      controller.memoryVaultStatus().value(QStringLiteral("activeStyleNotePath")).toString();
  const auto draft_style_memory_note = QString::fromUtf8(read_file_bytes(draft_style_memory_path));
  if (!draft_style_memory_note.contains(QStringLiteral("[[%1]]")
                                            .arg(QFileInfo(run_memory_path).completeBaseName())) ||
      !draft_style_memory_note.contains(QStringLiteral("[[%1]]")
                                            .arg(QFileInfo(issue_memory_path).completeBaseName()))) {
    return fail("style memory should backlink to run and issue notes that trained or changed the style");
  }
  const auto draft_memory_packet = controller.memoryPacketForPhase(QStringLiteral("batch"));
  if (draft_memory_packet.value(QStringLiteral("rawVaultIncluded")).toBool() ||
      !draft_memory_packet.value(QStringLiteral("includedMemoryKinds"))
           .toStringList()
           .contains(QStringLiteral("approved-sample-recipe")) ||
      !draft_memory_packet.value(QStringLiteral("includedMemoryKinds"))
           .toStringList()
           .contains(QStringLiteral("cluster-summary")) ||
      draft_memory_packet.value(QStringLiteral("memoryBudget"))
              .toMap()
              .value(QStringLiteral("approxTokenBudget"))
              .toInt() < 2000 ||
      !draft_memory_packet.value(QStringLiteral("approvedSampleRecipeCard"))
           .toMap()
           .value(QStringLiteral("available"))
           .toBool() ||
      draft_memory_packet.value(QStringLiteral("styleConstraintsCard"))
              .toMap()
              .value(QStringLiteral("allowedTools"))
              .toMap()
              .isEmpty() ||
      draft_memory_packet.value(QStringLiteral("lastRunCard"))
              .toMap()
              .value(QStringLiteral("runId"))
              .toString() != report.value(QStringLiteral("runId")).toString() ||
      !draft_memory_packet.value(QStringLiteral("latestIssueCard"))
           .toMap()
           .value(QStringLiteral("available"))
           .toBool()) {
    return fail("batch memory retrieval should include compact run and issue cards without dumping the memory vault");
  }
  if (report.value(QStringLiteral("durationMs")).toInt() < 0 ||
      !report.contains(QStringLiteral("queueWaitMs")) ||
      report.value(QStringLiteral("queueWaitMs")).toInt() < 0 ||
      report.value(QStringLiteral("toolCalls")).toList().size() < 3 ||
      report.value(QStringLiteral("performanceBudget"))
              .toMap()
              .value(QStringLiteral("budgetMs"))
              .toInt() <= 0 ||
      !report.value(QStringLiteral("performanceProofGate"))
           .toMap()
           .value(QStringLiteral("passed"))
           .toBool()) {
    return fail("draft report should include run timing, queue wait timing, performance gate, and structured tool call outcomes");
  }
  const auto report_tool_calls = report.value(QStringLiteral("toolCalls")).toList();
  const auto report_mcp_discovery = report.value(QStringLiteral("mcpDiscovery")).toMap();
  const auto report_mcp_observation = report.value(QStringLiteral("mcpObservation")).toMap();
  auto saw_mcp_discovery_tool_call = false;
  for (const auto& tool_call_value : report_tool_calls) {
    if (tool_call_value.toMap().value(QStringLiteral("name")).toString() ==
        QStringLiteral("mcp.tools/list")) {
      saw_mcp_discovery_tool_call = true;
      break;
    }
  }
  if (!saw_mcp_discovery_tool_call ||
      report.value(QStringLiteral("mcpAttached")).toBool() ||
      report.value(QStringLiteral("mcpToolCount")).toInt() != 0 ||
      report_mcp_discovery.value(QStringLiteral("mode")).toString() != QStringLiteral("mcp-readiness") ||
      report_mcp_discovery.value(QStringLiteral("ready")).toBool() ||
      report_mcp_discovery.value(QStringLiteral("attemptCount")).toInt() != 1 ||
      report_mcp_discovery.value(QStringLiteral("recoveryPolicy")).toString() !=
          QStringLiteral("retry-discovery-before-agent-planning") ||
      report_mcp_discovery.value(QStringLiteral("status")).toString() != QStringLiteral("Unavailable") ||
      report_mcp_observation.value(QStringLiteral("status")).toString() != QStringLiteral("unavailable") ||
      !report_mcp_observation.value(QStringLiteral("toolReports")).toList().isEmpty()) {
    return fail("draft report should audit MCP discovery/observation and degrade cleanly without a running endpoint");
  }
  if (!report.value(QStringLiteral("validationReport")).toMap().value(QStringLiteral("ok")).toBool()) {
    return fail("draft report should include a passing validation report");
  }
  const auto proof_render_policy =
      report.value(QStringLiteral("validationReport"))
          .toMap()
          .value(QStringLiteral("proofRenderPolicy"))
          .toMap();
  const auto draft_proof_source =
      report.value(QStringLiteral("validationReport"))
          .toMap()
          .value(QStringLiteral("proofRenderSource"))
          .toMap();
  const auto proof_render_checkpoints =
      proof_render_policy.value(QStringLiteral("checkpoints")).toStringList();
  if (!proof_render_policy.value(QStringLiteral("cancellable")).toBool() ||
      !proof_render_policy.value(QStringLiteral("timeoutAware")).toBool() ||
      !proof_render_checkpoints.contains(QStringLiteral("before-decode")) ||
      !proof_render_checkpoints.contains(QStringLiteral("before-validation"))) {
    return fail("draft validation should expose cancellable proof-render checkpoints");
  }
  if (draft_proof_source.value(QStringLiteral("sourceResolutionPolicy")).toString() !=
          QStringLiteral("bounded-preview") ||
      draft_proof_source.value(QStringLiteral("sourceDecodeMaxEdge")).toInt() <= 0 ||
      !draft_proof_source.value(QStringLiteral("usesDecodedImageCache")).toBool() ||
      draft_proof_source.value(QStringLiteral("fullResolutionRequired")).toBool()) {
    return fail("draft validation should use a bounded cached preview source for proof rendering");
  }
  const auto preview_proof_policy = report.value(QStringLiteral("previewProofPolicy")).toMap();
  const auto preview_asset_reports = report.value(QStringLiteral("assetReports")).toList();
  const auto preview_first_asset_report =
      preview_asset_reports.isEmpty() ? QVariantMap{} : preview_asset_reports.first().toMap();
  if (!preview_proof_policy.value(QStringLiteral("draftPreviewsDuringPlanning")).toBool() ||
      preview_proof_policy.value(QStringLiteral("draftPreviewCount")).toInt() <= 0 ||
      !preview_proof_policy.value(QStringLiteral("settledPreviewsForFinalProof")).toBool() ||
      preview_proof_policy.value(QStringLiteral("settledPreviewCount")).toInt() <= 0 ||
      !preview_first_asset_report.value(QStringLiteral("usesDraftPreviewsForPlanning")).toBool() ||
      preview_first_asset_report.value(QStringLiteral("draftPreviewCount")).toInt() <= 0 ||
      !preview_first_asset_report.value(QStringLiteral("usesSettledPreviewForFinalProof")).toBool() ||
      !preview_first_asset_report.value(QStringLiteral("settledPreviewReady")).toBool()) {
    return fail("draft report should prove iterative draft previews and settled final proof previews");
  }
  if (report.value(QStringLiteral("planSummary")).toString().isEmpty()) {
    return fail("draft report should include model plan summary");
  }
  if (report.value(QStringLiteral("editGoals")).toList().isEmpty() ||
      !report.value(QStringLiteral("riskAssessment")).toMap().contains(QStringLiteral("riskCount"))) {
    return fail("draft report should include explicit edit goals and risk assessment");
  }
  if (!report.value(QStringLiteral("reviewRequired")).toBool() ||
      report.value(QStringLiteral("qualityGuarantee")).toBool() ||
      report.value(QStringLiteral("reviewRequiredReason")).toString().isEmpty()) {
    return fail("draft report should require user review without promising perfect taste");
  }
  if (report.value(QStringLiteral("modelBackend")).toString() != QStringLiteral("local") ||
      !QFileInfo(report.value(QStringLiteral("manifestPath")).toString()).isFile()) {
    return fail("draft report should include model backend and persist a run manifest");
  }
  if (report.value(QStringLiteral("modelRunnerMode")).toString() != QStringLiteral("worker-thread") ||
      report.value(QStringLiteral("uiEventPumpCount")).toInt() < 0) {
    return fail("draft report should show that model planning ran on the worker runner");
  }
  const auto report_prompt_audit = report.value(QStringLiteral("modelPromptAudit")).toMap();
  if (report_prompt_audit.value(QStringLiteral("strategyPhase")).toString() != QStringLiteral("sample") ||
      !report_prompt_audit.value(QStringLiteral("deterministicPlanning")).toBool() ||
      report_prompt_audit.value(QStringLiteral("temperature")).toDouble() != 0.0 ||
      report_prompt_audit.value(QStringLiteral("memoryPacketPhase")).toString() != QStringLiteral("sample") ||
      report_prompt_audit.value(QStringLiteral("rawMemoryVaultIncluded")).toBool() ||
      !report_prompt_audit.value(QStringLiteral("includedMemoryKinds"))
           .toStringList()
           .contains(QStringLiteral("style")) ||
      !report_prompt_audit.value(QStringLiteral("strategyPrompt"))
           .toString()
           .contains(QStringLiteral("Phase: sample"))) {
    return fail("draft planning should use a deterministic phase-specific strategy prompt with compact memory context");
  }
  if (controller.recipeBaseMode() != QStringLiteral("current") ||
      report.value(QStringLiteral("recipeBaseMode")).toString() != QStringLiteral("current") ||
      report.value(QStringLiteral("manualEditPolicy")).toString() !=
          QStringLiteral("preserve-unspecified-current-recipe-fields")) {
    return fail("default agent draft should preserve unspecified fields from the current recipe");
  }
  const auto style_citations = report.value(QStringLiteral("styleConstraintCitations")).toList();
  if (style_citations.isEmpty() ||
      !has_citation_field(style_citations, QStringLiteral("constraints.max_exposure_shift")) ||
      !has_citation_field(
          style_citations,
          QStringLiteral("validation.highlight_clip_limit_percent"))) {
    return fail("draft report should include model style constraint citations");
  }
  const auto artifact_directory = report.value(QStringLiteral("artifactDirectory")).toString();
  const auto before_proof_path = report.value(QStringLiteral("beforeProofPath")).toString();
  const auto after_proof_path = report.value(QStringLiteral("afterProofPath")).toString();
  const auto recipe_diff_path = report.value(QStringLiteral("recipeDiffPath")).toString();
  const auto validation_report_path = report.value(QStringLiteral("validationReportPath")).toString();
  if (!QFileInfo::exists(artifact_directory) ||
      !QFileInfo(before_proof_path).isFile() ||
      !QFileInfo(after_proof_path).isFile() ||
      !QFileInfo(recipe_diff_path).isFile() ||
      !QFileInfo(validation_report_path).isFile()) {
    return fail("agent draft should persist before/after proof, recipe diff, and validation artifacts");
  }
  const auto recipe_diff = report.value(QStringLiteral("recipeDiff")).toMap();
  const auto exposure_diff = recipe_diff.value(QStringLiteral("globalTone"))
                                 .toMap()
                                 .value(QStringLiteral("exposure"))
                                 .toMap();
  if (exposure_diff.isEmpty() ||
      exposure_diff.value(QStringLiteral("before")).toDouble() != original_state.exposure ||
      exposure_diff.value(QStringLiteral("after")).toDouble() <= original_state.exposure) {
    return fail("draft report should include a grouped before/after recipe diff");
  }
  const auto persisted_diff = read_json_object(recipe_diff_path);
  if (!persisted_diff.contains(QStringLiteral("globalTone"))) {
    return fail("recipe diff artifact should contain grouped edit changes");
  }
  const auto persisted_validation = read_json_object(validation_report_path);
  if (!persisted_validation.value(QStringLiteral("ok")).toBool()) {
    return fail("validation artifact should contain a passing report");
  }
  const auto draft_manifest = read_json_object(report.value(QStringLiteral("manifestPath")).toString());
  const auto draft_prompt_audit = draft_manifest.value(QStringLiteral("modelPromptAudit")).toObject();
  if (draft_manifest.value(QStringLiteral("mode")).toString() != QStringLiteral("draft") ||
      draft_manifest.value(QStringLiteral("styleConstraintCitations")).toArray().isEmpty() ||
      draft_manifest.value(QStringLiteral("editGoals")).toArray().isEmpty() ||
      draft_prompt_audit.value(QStringLiteral("backend")).toString() != QStringLiteral("local") ||
      !draft_prompt_audit.value(QStringLiteral("userPrompt")).toString().contains(controller.selectedStyleId()) ||
      draft_prompt_audit.value(QStringLiteral("userPromptCharCount")).toInt() <= 0 ||
      !draft_manifest.value(QStringLiteral("riskAssessment")).toObject().contains(QStringLiteral("riskCount")) ||
      draft_manifest.value(QStringLiteral("toolCalls")).toArray().size() < 3 ||
      draft_manifest.value(QStringLiteral("durationMs")).toInt(-1) < 0 ||
      draft_manifest.value(QStringLiteral("queueWaitMs")).toInt(-1) < 0 ||
      draft_manifest.value(QStringLiteral("performanceBudget"))
              .toObject()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("within-budget") ||
      !draft_manifest.value(QStringLiteral("performanceProofGate"))
           .toObject()
           .value(QStringLiteral("passed"))
           .toBool() ||
      draft_manifest.value(QStringLiteral("modelRunnerMode")).toString() !=
          QStringLiteral("worker-thread") ||
      !draft_manifest.value(QStringLiteral("reviewRequired")).toBool() ||
      draft_manifest.value(QStringLiteral("qualityGuarantee")).toBool() ||
      draft_manifest.value(QStringLiteral("recipeBaseMode")).toString() != QStringLiteral("current") ||
      draft_manifest.value(QStringLiteral("manualEditPolicy")).toString() !=
          QStringLiteral("preserve-unspecified-current-recipe-fields") ||
      draft_manifest.value(QStringLiteral("recipeStorageMode")).toString() !=
          QStringLiteral("recipe-diff-first") ||
      !draft_manifest.value(QStringLiteral("previewProofPolicy"))
           .toObject()
           .value(QStringLiteral("draftPreviewsDuringPlanning"))
           .toBool() ||
      !draft_manifest.value(QStringLiteral("previewProofPolicy"))
           .toObject()
           .value(QStringLiteral("settledPreviewsForFinalProof"))
           .toBool() ||
      draft_manifest.value(QStringLiteral("recipeDiff")).toObject().isEmpty() ||
      draft_manifest.value(QStringLiteral("model"))
              .toObject()
              .value(QStringLiteral("backend"))
              .toString() != QStringLiteral("local")) {
    return fail("draft manifest should persist mode, model metadata, prompt audit, style citations, preview proof policy, and performance proof gate");
  }
  const auto shareable_manifest_path = report.value(QStringLiteral("shareableManifestPath")).toString();
  const auto shareable_manifest_bytes = read_file_bytes(shareable_manifest_path);
  const auto shareable_manifest = QJsonDocument::fromJson(shareable_manifest_bytes).object();
  const auto shareable_prompt_audit =
      shareable_manifest.value(QStringLiteral("modelPromptAudit")).toObject();
  if (shareable_manifest_path.isEmpty() ||
      !QFileInfo(shareable_manifest_path).isFile() ||
      !shareable_manifest.value(QStringLiteral("redacted")).toBool() ||
      shareable_manifest.value(QStringLiteral("mode")).toString() != QStringLiteral("draft") ||
      shareable_manifest.value(QStringLiteral("styleConstraintCitations")).toArray().isEmpty() ||
      !shareable_prompt_audit.value(QStringLiteral("promptTextRedacted")).toBool() ||
      shareable_prompt_audit.value(QStringLiteral("userPrompt")).toString() !=
          QStringLiteral("<redacted-prompt>")) {
    return fail("draft run should persist a redacted shareable manifest with useful run metadata");
  }
  if (shareable_manifest_bytes.contains(runtime_root.path().toUtf8()) ||
      shareable_manifest_bytes.contains(source_path.toUtf8()) ||
      shareable_manifest_bytes.contains(before_proof_path.toUtf8()) ||
      !shareable_manifest_bytes.contains("<app-data>")) {
    return fail("shareable draft manifest should redact absolute app-data and source paths");
  }

  const auto drafted_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.selectedRecipeJson());
  if (drafted_state.exposure <= original_state.exposure) {
    return fail("agent draft should update exposure");
  }
  if (drafted_state.temperature <= original_state.temperature) {
    return fail("agent draft should update white balance temperature");
  }
  if (drafted_state.output_color_space != original_state.output_color_space) {
    return fail("current-base agent draft should preserve manual recipe fields outside the patch");
  }
  if (drafted_state.recipe_snapshots.empty()) {
    return fail("agent draft should snapshot the original recipe before applying changes");
  }
  const auto agent_snapshot = drafted_state.recipe_snapshots.back().toMap();
  if (!agent_snapshot.value(QStringLiteral("agentGenerated")).toBool() ||
      agent_snapshot.value(QStringLiteral("agentRunId")).toString() != report.value(QStringLiteral("runId")).toString() ||
      agent_snapshot.value(QStringLiteral("agentModelId")).toString() != report.value(QStringLiteral("modelId")).toString() ||
      agent_snapshot.value(QStringLiteral("agentStyleId")).toString() != controller.selectedStyleId() ||
      agent_snapshot.value(QStringLiteral("agentCreatedAtUtc")).toString().isEmpty()) {
    return fail("agent draft snapshot should include run, model, style, and timestamp provenance");
  }
  if (report.value(QStringLiteral("recipeStorageMode")).toString() != QStringLiteral("recipe-diff-first") ||
      agent_snapshot.value(QStringLiteral("agentChangeStorage")).toString() !=
          QStringLiteral("recipe-diff-first") ||
      agent_snapshot.value(QStringLiteral("agentRecipePatch")).toMap().isEmpty() ||
      agent_snapshot.value(QStringLiteral("agentRecipeDiff")).toMap().isEmpty()) {
    return fail("agent draft should store change provenance as recipe diffs before full snapshots");
  }

  const auto user_revision = controller.runDraftOnSelected(QStringLiteral("revise the draft warmer"));
  if (!user_revision.value(QStringLiteral("ok")).toBool() ||
      user_revision.value(QStringLiteral("runId")).toString() != report.value(QStringLiteral("runId")).toString() ||
      !QFileInfo(user_revision.value(QStringLiteral("afterProofPath")).toString()).isFile() ||
      !controller.draftActive()) {
    return fail("user-requested draft revision should reuse the active draft transaction");
  }
  const auto revised_drafted_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.selectedRecipeJson());
  if (revised_drafted_state.recipe_snapshots.size() < drafted_state.recipe_snapshots.size()) {
    return fail("user-requested draft revision should preserve agent draft provenance");
  }

  const auto rollback = controller.rollbackDraft();
  if (!rollback.value(QStringLiteral("ok")).toBool() ||
      rollback.value(QStringLiteral("mode")).toString() != QStringLiteral("rollback")) {
    return fail("rollback should succeed");
  }
  run_history = controller.runHistory();
  if (run_history.isEmpty() ||
      run_history.first().toMap().value(QStringLiteral("mode")).toString() != QStringLiteral("rollback")) {
    return fail("agent run history should record draft rollbacks as the latest run event");
  }
  if (controller.draftActive()) {
    return fail("rollback should clear the active draft");
  }

  const auto restored_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.selectedRecipeJson());
  if (restored_state.exposure != original_state.exposure ||
      restored_state.temperature != original_state.temperature ||
      restored_state.saturation != original_state.saturation) {
    return fail("rollback should restore the original recipe values");
  }

  controller.setRecipeBaseMode(QStringLiteral("neutral"));
  const auto neutral_report = controller.runDraftOnSelected(QStringLiteral("clean neutral draft"));
  if (!neutral_report.value(QStringLiteral("ok")).toBool() ||
      neutral_report.value(QStringLiteral("recipeBaseMode")).toString() != QStringLiteral("neutral") ||
      neutral_report.value(QStringLiteral("manualEditPolicy")).toString() !=
          QStringLiteral("replace-current-edits-with-neutral-base")) {
    return fail("neutral-base agent draft should report that it replaces current edits with a neutral base");
  }
  const auto neutral_drafted_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.selectedRecipeJson());
  const photo_editor::desktop::EditRecipeState neutral_defaults;
  if (neutral_drafted_state.output_color_space != neutral_defaults.output_color_space) {
    return fail("neutral-base agent draft should start from neutral recipe fields outside the patch");
  }
  const auto neutral_snapshot = neutral_drafted_state.recipe_snapshots.back().toMap();
  const auto neutral_snapshot_state =
      photo_editor::desktop::parse_edit_recipe_json(neutral_snapshot.value(QStringLiteral("recipeJson")).toString());
  if (neutral_snapshot_state.output_color_space != original_state.output_color_space) {
    return fail("neutral-base agent draft should still snapshot the original current recipe for rollback");
  }
  const auto neutral_rollback = controller.rollbackDraft();
  if (!neutral_rollback.value(QStringLiteral("ok")).toBool()) {
    return fail("neutral-base draft rollback should succeed");
  }
  controller.setRecipeBaseMode(QStringLiteral("current"));

  catalog_model.toggleSelectionIndex(1);
  const auto recipe_before_batch_validation = catalog_model.selectedRecipeJson();
  const auto batch_validation = controller.validateBatchForSelected();
  if (!batch_validation.value(QStringLiteral("ok")).toBool() ||
      batch_validation.value(QStringLiteral("mode")).toString() != QStringLiteral("validate-batch") ||
      batch_validation.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      batch_validation.value(QStringLiteral("checkedCount")).toInt() != 2 ||
      batch_validation.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1 ||
      batch_validation.value(QStringLiteral("assetReports")).toList().size() != 2 ||
      !QFileInfo(batch_validation.value(QStringLiteral("batchValidationReportPath")).toString()).isFile() ||
      !QFileInfo(batch_validation.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.draftActive()) {
    return fail("selected-set batch validation should persist per-asset reports without creating a draft");
  }
  for (const auto& asset_report_value : batch_validation.value(QStringLiteral("assetReports")).toList()) {
    const auto asset_report = asset_report_value.toMap();
    if (asset_report.value(QStringLiteral("status")).toString() != QStringLiteral("validated") ||
        !QFileInfo(asset_report.value(QStringLiteral("currentProofPath")).toString()).isFile() ||
        !QFileInfo(asset_report.value(QStringLiteral("analysisReportPath")).toString()).isFile() ||
        !asset_report.value(QStringLiteral("validationReport")).toMap().value(QStringLiteral("ok")).toBool()) {
      return fail("selected-set batch validation should persist each asset proof and validation report");
    }
  }
  if (catalog_model.selectedRecipeJson() != recipe_before_batch_validation) {
    return fail("selected-set batch validation should not mutate recipes");
  }

  const auto disallowed_global_crop = controller.applyDraftRecipePatchToSelected(
      QVariantMap{
          {QStringLiteral("crop"),
           QVariantMap{
               {QStringLiteral("cropZoom"), 1.1},
           }},
      });
  if (disallowed_global_crop.value(QStringLiteral("ok")).toBool() ||
      controller.draftActive() ||
      !disallowed_global_crop.value(QStringLiteral("errors"))
           .toStringList()
           .join(QStringLiteral("; "))
           .contains(QStringLiteral("cannot apply crop edits globally"))) {
    return fail("selected-set drafts should reject global crop edits unless style batch behavior allows them");
  }

  const auto missing_second_source_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/missing-source-2.png"));
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), missing_second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.selectAll();
  const QVariantMap resumable_patch{
      {QStringLiteral("globalTone"),
       QVariantMap{
           {QStringLiteral("exposure"), 0.18},
           {QStringLiteral("contrast"), 4.0},
       }},
  };
  const auto resumable_failure = controller.applyDraftRecipePatchToSelected(resumable_patch);
  const auto resume_plan = resumable_failure.value(QStringLiteral("resumePlan")).toMap();
  if (resumable_failure.value(QStringLiteral("ok")).toBool() ||
      !resume_plan.value(QStringLiteral("available")).toBool() ||
      !resume_plan.value(QStringLiteral("cleanAssetIds")).toStringList().contains(QStringLiteral("asset-1")) ||
      resume_plan.value(QStringLiteral("cleanAssetReports")).toList().isEmpty() ||
      controller.draftActive() ||
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString()).exposure !=
          original_state.exposure) {
    return fail("failed batch draft should persist a resume plan after the last clean asset without applying recipes");
  }
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.selectAll();
  const auto resumed_batch = controller.resumeLastFailedBatchDraft();
  const auto resumed_asset_reports = resumed_batch.value(QStringLiteral("assetReports")).toList();
  auto reused_clean_validation = false;
  for (const auto& report_value : resumed_asset_reports) {
    const auto asset_report = report_value.toMap();
    if (asset_report.value(QStringLiteral("assetId")).toString() == QStringLiteral("asset-1") &&
        asset_report.value(QStringLiteral("validationSkippedByResume")).toBool()) {
      reused_clean_validation = true;
      break;
    }
  }
  const auto resumed_first_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString());
  const auto resumed_second_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(1).value(QStringLiteral("recipeJson")).toString());
  const auto resumed_manifest =
      read_json_object(resumed_batch.value(QStringLiteral("manifestPath")).toString());
  if (!resumed_batch.value(QStringLiteral("ok")).toBool() ||
      !resumed_batch.value(QStringLiteral("resumed")).toBool() ||
      resumed_batch.value(QStringLiteral("resumedFromRunId")).toString() !=
          resumable_failure.value(QStringLiteral("runId")).toString() ||
      resumed_asset_reports.size() != 2 ||
      !reused_clean_validation ||
      resumed_first_state.exposure <= original_state.exposure ||
      resumed_second_state.exposure <= second_original_state.exposure ||
      !resumed_manifest.value(QStringLiteral("resumed")).toBool() ||
      resumed_manifest.value(QStringLiteral("resumedFromRunId")).toString() !=
          resumable_failure.value(QStringLiteral("runId")).toString() ||
      !controller.draftActive()) {
    return fail("resume should continue a failed batch from the last clean asset and draft the full selection");
  }
  const auto resumed_rollback = controller.rollbackDraft();
  if (!resumed_rollback.value(QStringLiteral("ok")).toBool() || controller.draftActive()) {
    return fail("resumed batch draft should remain rollbackable");
  }
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.selectAll();

  const auto batch_report = controller.runDraftOnSelected(QStringLiteral("apply the style consistently"));
  if (!batch_report.value(QStringLiteral("ok")).toBool()) {
    std::cerr << batch_report.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString()
              << '\n';
    return fail("agent selected-set draft should succeed");
  }
  if (batch_report.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      batch_report.value(QStringLiteral("draftedCount")).toInt() != 2 ||
      batch_report.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1 ||
      batch_report.value(QStringLiteral("assetReports")).toList().size() != 2) {
    return fail("selected-set draft should report batch scope and per-asset results");
  }
  const auto batch_summary = batch_report.value(QStringLiteral("batchSummary")).toMap();
  const auto batch_status_counts = batch_summary.value(QStringLiteral("statusCounts")).toMap();
  const auto batch_review_summary = batch_summary.value(QStringLiteral("reviewSummary")).toMap();
  const auto batch_transaction = batch_report.value(QStringLiteral("batchTransaction")).toMap();
  const auto batch_clusters = batch_report.value(QStringLiteral("batchClusters")).toList();
  const auto batch_cluster_plans = batch_report.value(QStringLiteral("clusterPlanning")).toList();
  const auto sample_review = batch_report.value(QStringLiteral("sampleReview")).toMap();
  const auto sample_review_samples = sample_review.value(QStringLiteral("samples")).toList();
  const auto first_sample =
      sample_review_samples.isEmpty() ? QVariantMap{} : sample_review_samples.first().toMap();
  const auto sample_review_actions = sample_review.value(QStringLiteral("reviewActions")).toList();
  if (batch_summary.value(QStringLiteral("assetCount")).toInt() != 2 ||
      batch_summary.value(QStringLiteral("clusterCount")).toInt() != 2 ||
      batch_status_counts.value(QStringLiteral("drafted")).toInt() != 2 ||
      batch_summary.value(QStringLiteral("proofCount")).toInt() != 2 ||
      batch_summary.value(QStringLiteral("recipeDiffCount")).toInt() != 2 ||
      batch_review_summary.value(QStringLiteral("editedCount")).toInt() != 2 ||
      batch_review_summary.value(QStringLiteral("failedCount")).toInt() != 0 ||
      batch_review_summary.value(QStringLiteral("passedCount")).toInt() +
              batch_review_summary.value(QStringLiteral("needsReviewCount")).toInt() != 2 ||
      batch_review_summary.value(QStringLiteral("issueCount")).toInt() !=
          batch_summary.value(QStringLiteral("reviewIssues")).toList().size() ||
      batch_review_summary.value(QStringLiteral("showOnlyIssuesFirst")).toBool() !=
          !batch_summary.value(QStringLiteral("reviewIssues")).toList().isEmpty() ||
      batch_clusters.size() != 2 ||
      batch_cluster_plans.size() != 2 ||
      batch_report.value(QStringLiteral("representativeAssetIds")).toStringList().size() != 2 ||
      !batch_summary.value(QStringLiteral("readyForCommit")).toBool() ||
      !batch_summary.value(QStringLiteral("confirmationRequired")).toBool() ||
      batch_transaction.value(QStringLiteral("transactionId")).toString() !=
          batch_report.value(QStringLiteral("runId")).toString() ||
      batch_transaction.value(QStringLiteral("status")).toString() != QStringLiteral("drafted") ||
      !batch_transaction.value(QStringLiteral("rollbackAvailable")).toBool() ||
      batch_transaction.value(QStringLiteral("assetCount")).toInt() != 2) {
    return fail("selected-set draft should expose a pre-commit batch summary");
  }
  if (!sample_review.value(QStringLiteral("available")).toBool() ||
      !sample_review.value(QStringLiteral("sampleFirst")).toBool() ||
      !sample_review.value(QStringLiteral("showOnlySamplesFirst")).toBool() ||
      sample_review.value(QStringLiteral("sampleAssetIds")).toStringList().size() != 2 ||
      sample_review.value(QStringLiteral("visibleAssetIds")).toStringList() !=
          sample_review.value(QStringLiteral("sampleAssetIds")).toStringList() ||
      sample_review_samples.size() != 2 ||
      first_sample.value(QStringLiteral("badge")).toString().isEmpty() ||
      first_sample.value(QStringLiteral("explanation")).toString().isEmpty() ||
      !QFileInfo(first_sample.value(QStringLiteral("beforeProofPath")).toString()).isFile() ||
      !QFileInfo(first_sample.value(QStringLiteral("afterProofPath")).toString()).isFile() ||
      sample_review_actions.size() < 4 ||
      sample_review_actions.at(0).toMap().value(QStringLiteral("label")).toString() !=
          QStringLiteral("Looks good") ||
      sample_review_actions.at(1).toMap().value(QStringLiteral("label")).toString() !=
          QStringLiteral("Make changes") ||
      sample_review_actions.at(2).toMap().value(QStringLiteral("label")).toString() !=
          QStringLiteral("Reject") ||
      sample_review_actions.at(3).toMap().value(QStringLiteral("label")).toString() !=
          QStringLiteral("Apply to All")) {
    return fail("selected-set draft should expose a sample-first review packet with proofs, badges, explanations, and review actions");
  }
  QFile batch_manifest_file(batch_report.value(QStringLiteral("manifestPath")).toString());
  if (!batch_manifest_file.open(QIODevice::ReadOnly)) {
    return fail("selected-set draft manifest should be readable");
  }
  const auto batch_manifest_document =
      QJsonDocument::fromJson(batch_manifest_file.readAll()).object();
  const auto batch_prompt_audit =
      batch_manifest_document.value(QStringLiteral("modelPromptAudit")).toObject();
  const auto batch_cluster_planning =
      batch_manifest_document.value(QStringLiteral("clusterPlanning")).toArray();
  const auto first_cluster_prompt_audit =
      batch_cluster_planning.isEmpty()
          ? QJsonObject{}
          : batch_cluster_planning.first().toObject().value(QStringLiteral("modelPromptAudit")).toObject();
  if (batch_manifest_document.value(QStringLiteral("batchSummary"))
          .toObject()
          .value(QStringLiteral("assetCount"))
          .toInt() != 2 ||
      batch_manifest_document.value(QStringLiteral("batchClusters")).toArray().size() != 2 ||
      batch_cluster_planning.size() != 2 ||
      batch_prompt_audit.value(QStringLiteral("backend")).toString() != QStringLiteral("local") ||
      first_cluster_prompt_audit.value(QStringLiteral("backend")).toString() != QStringLiteral("local") ||
      batch_manifest_document.value(QStringLiteral("batchTransaction"))
              .toObject()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("drafted") ||
      batch_manifest_document.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1) {
    return fail("selected-set draft manifest should persist the pre-commit batch summary, prompt audits, and transaction");
  }
  const auto batch_first_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString());
  const auto batch_second_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(1).value(QStringLiteral("recipeJson")).toString());
  if (batch_first_state.exposure <= original_state.exposure ||
      batch_second_state.exposure <= second_original_state.exposure) {
    return fail("selected-set draft should update every selected recipe");
  }
  const auto batch_asset_reports = batch_report.value(QStringLiteral("assetReports")).toList();
  for (const auto& asset_report_value : batch_asset_reports) {
    const auto asset_report = asset_report_value.toMap();
    const auto validation_report = asset_report.value(QStringLiteral("validationReport")).toMap();
    const auto artifacts = validation_report.value(QStringLiteral("artifacts")).toMap();
    if (asset_report.value(QStringLiteral("status")).toString() != QStringLiteral("drafted") ||
        asset_report.value(QStringLiteral("clusterId")).toString().isEmpty() ||
        asset_report.value(QStringLiteral("clusterRepresentativeAssetId")).toString().isEmpty() ||
        asset_report.value(QStringLiteral("clusterSyncGroups")).toStringList().isEmpty() ||
        !QFileInfo(artifacts.value(QStringLiteral("afterProofPath")).toString()).isFile() ||
        validation_report.value(QStringLiteral("recipeDiff")).toMap().isEmpty()) {
      return fail("selected-set draft should persist per-asset cluster metadata, proof artifacts, and diffs");
    }
  }
  const auto initial_review_issues = batch_report.value(QStringLiteral("batchSummary"))
                                         .toMap()
                                         .value(QStringLiteral("reviewIssues"))
                                         .toList();
  if (initial_review_issues.isEmpty()) {
    return fail("selected-set draft should expose issue-first review rows when validation warnings are present");
  }
  const auto first_review_issue = initial_review_issues.first().toMap();
  if (first_review_issue.value(QStringLiteral("thumbnailPath")).toString().isEmpty() ||
      first_review_issue.value(QStringLiteral("reason")).toString().isEmpty() ||
      first_review_issue.value(QStringLiteral("suggestedAction")).toString().isEmpty() ||
      !first_review_issue.contains(QStringLiteral("warningCount")) ||
      !first_review_issue.contains(QStringLiteral("errorCount")) ||
      !first_review_issue.contains(QStringLiteral("styleMatchScorePercent"))) {
    return fail("review issue rows should include proof thumbnail, reason, suggested action, and expandable detail fields");
  }
  const auto initial_review_summary = batch_report.value(QStringLiteral("batchSummary"))
                                          .toMap()
                                          .value(QStringLiteral("reviewSummary"))
                                          .toMap();
  if (initial_review_issues.size() < 2 ||
      !initial_review_summary.value(QStringLiteral("fixAllOutliersAvailable")).toBool()) {
    return fail("selected-set draft should offer safe bulk fixing when multiple review issues have no failures");
  }
  const auto fixed_all_issues = controller.fixAllReviewIssues();
  const auto fixed_all_summary =
      fixed_all_issues.value(QStringLiteral("batchSummary")).toMap().value(QStringLiteral("reviewSummary")).toMap();
  if (!fixed_all_issues.value(QStringLiteral("ok")).toBool() ||
      fixed_all_issues.value(QStringLiteral("mode")).toString() != QStringLiteral("review-issue-fix-all") ||
      fixed_all_issues.value(QStringLiteral("fixedCount")).toInt() != initial_review_issues.size() ||
      fixed_all_issues.value(QStringLiteral("repairReports")).toList().size() != initial_review_issues.size() ||
      fixed_all_summary.value(QStringLiteral("editedCount")).toInt() != 2 ||
      !controller.draftActive()) {
    return fail("fixing all review issues should repair every safe issue and keep the batch draft active");
  }
  const auto review_issue_asset_id =
      initial_review_issues.first().toMap().value(QStringLiteral("assetId")).toString();
  const auto fixed_issue = controller.fixReviewIssue(review_issue_asset_id);
  const auto fixed_issue_summary =
      fixed_issue.value(QStringLiteral("batchSummary")).toMap().value(QStringLiteral("reviewSummary")).toMap();
  if (!fixed_issue.value(QStringLiteral("ok")).toBool() ||
      fixed_issue.value(QStringLiteral("mode")).toString() != QStringLiteral("review-issue-fix") ||
      fixed_issue.value(QStringLiteral("assetId")).toString() != review_issue_asset_id ||
      fixed_issue.value(QStringLiteral("resolution")).toString() != QStringLiteral("fixed") ||
      fixed_issue.value(QStringLiteral("repairedRecipePatch")).toMap().isEmpty() ||
      fixed_issue_summary.value(QStringLiteral("editedCount")).toInt() != 2 ||
      !controller.draftActive()) {
    return fail("fixing a review issue should re-draft only that photo and keep the batch review active");
  }
  const auto accepted_issue = controller.acceptReviewIssue(review_issue_asset_id);
  const auto accepted_issue_summary =
      accepted_issue.value(QStringLiteral("batchSummary")).toMap().value(QStringLiteral("reviewSummary")).toMap();
  if (!accepted_issue.value(QStringLiteral("ok")).toBool() ||
      accepted_issue.value(QStringLiteral("resolution")).toString() != QStringLiteral("accepted_anyway") ||
      accepted_issue_summary.value(QStringLiteral("issueCount")).toInt() >= initial_review_issues.size() ||
      !controller.draftActive()) {
    return fail("accepting a review issue should resolve it without changing the active draft");
  }
  const auto rolled_back_issue = controller.rollbackReviewIssue(review_issue_asset_id);
  const auto rolled_back_issue_summary =
      rolled_back_issue.value(QStringLiteral("batchSummary")).toMap().value(QStringLiteral("reviewSummary")).toMap();
  const auto rolled_back_issue_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString());
  if (!rolled_back_issue.value(QStringLiteral("ok")).toBool() ||
      rolled_back_issue.value(QStringLiteral("resolution")).toString() != QStringLiteral("rolled_back") ||
      rolled_back_issue.value(QStringLiteral("restoredCount")).toInt() != 1 ||
      rolled_back_issue_summary.value(QStringLiteral("editedCount")).toInt() != 1 ||
      rolled_back_issue_summary.value(QStringLiteral("issueCount")).toInt() >= initial_review_issues.size() ||
      rolled_back_issue_state.exposure != original_state.exposure ||
      !controller.draftActive()) {
    return fail("rolling back one review issue should restore that photo and keep the remaining batch draft active");
  }
  const auto batch_rollback = controller.rollbackDraft();
  if (!batch_rollback.value(QStringLiteral("ok")).toBool() ||
      batch_rollback.value(QStringLiteral("restoredCount")).toInt() != 1 ||
      batch_rollback.value(QStringLiteral("batchTransaction"))
              .toMap()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("rolled_back") ||
      batch_rollback.value(QStringLiteral("batchTransaction"))
              .toMap()
              .value(QStringLiteral("assetCount"))
              .toInt() != 1) {
    return fail("selected-set rollback should restore the remaining active draft recipes and report the transaction");
  }
  const auto restored_first_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString());
  const auto restored_second_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(1).value(QStringLiteral("recipeJson")).toString());
  if (restored_first_state.exposure != original_state.exposure ||
      restored_second_state.exposure != second_original_state.exposure) {
    return fail("selected-set rollback should restore each asset's original recipe");
  }

  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
      make_asset(QStringLiteral("asset-3"), third_source_path, third_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.selectAll();
  const auto sample_first_batch =
      controller.runDraftOnSelected(QStringLiteral("create wedding samples before finishing the full set"));
  const auto sample_first_review = sample_first_batch.value(QStringLiteral("sampleReview")).toMap();
  if (!sample_first_batch.value(QStringLiteral("ok")).toBool() ||
      !sample_first_batch.value(QStringLiteral("sampleOnlyDraft")).toBool() ||
      sample_first_batch.value(QStringLiteral("draftedCount")).toInt() != 2 ||
      sample_first_batch.value(QStringLiteral("fullTargetCount")).toInt() != 3 ||
      sample_first_batch.value(QStringLiteral("remainingAssetIds")).toStringList() !=
          QStringList{QStringLiteral("asset-3")} ||
      !sample_first_review.value(QStringLiteral("applyToAllAvailable")).toBool() ||
      sample_first_review.value(QStringLiteral("sampleAssetIds")).toStringList().size() != 2 ||
      sample_first_review.value(QStringLiteral("remainingAssetIds")).toStringList() !=
          QStringList{QStringLiteral("asset-3")} ||
      sample_first_review.value(QStringLiteral("fullTargetCount")).toInt() != 3 ||
      sample_first_batch.value(QStringLiteral("plannedPatchesByAssetId")).toMap().size() != 3 ||
      !controller.draftActive()) {
    return fail("larger selected-set draft should create representative samples before applying the full batch");
  }
  const auto sample_waiting_ai_state = controller.aiEditWorkflowState();
  if (sample_waiting_ai_state.value(QStringLiteral("statusLine")).toString() !=
          QStringLiteral("Waiting for your review") ||
      !sample_waiting_ai_state.value(QStringLiteral("reviewRequired")).toBool() ||
      !sample_waiting_ai_state.value(QStringLiteral("sampleReviewWaiting")).toBool() ||
      sample_waiting_ai_state.value(QStringLiteral("sampleRemainingCount")).toInt() != 1 ||
      !sample_waiting_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("confirmationRequired")).toBool() ||
      sample_waiting_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("key")).toString() !=
          QStringLiteral("apply_to_all") ||
      sample_waiting_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("label")).toString() !=
          QStringLiteral("Apply to All")) {
    return fail("AI edit workflow state should make a sample-only draft wait for Apply to All review");
  }
  const auto sample_first_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString());
  const auto sample_second_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(1).value(QStringLiteral("recipeJson")).toString());
  const auto sample_third_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(2).value(QStringLiteral("recipeJson")).toString());
  if (sample_first_state.exposure <= original_state.exposure ||
      sample_second_state.exposure <= second_original_state.exposure ||
      sample_third_state.exposure != third_original_state.exposure) {
    return fail("sample-first batch should draft only representative sample recipes before approval");
  }
  const auto sample_source_run_id = sample_first_batch.value(QStringLiteral("runId")).toString();
  const auto revised_sample_batch =
      controller.reviseSampleReview(QStringLiteral("Make the samples warmer and keep dress highlights softer."));
  const auto revised_sample_ai_state = controller.aiEditWorkflowState();
  const auto revised_third_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(2).value(QStringLiteral("recipeJson")).toString());
  if (!revised_sample_batch.value(QStringLiteral("ok")).toBool() ||
      !revised_sample_batch.value(QStringLiteral("sampleRevision")).toBool() ||
      revised_sample_batch.value(QStringLiteral("revisedFromRunId")).toString() != sample_source_run_id ||
      revised_sample_batch.value(QStringLiteral("sampleRevisionFeedback")).toString() !=
          QStringLiteral("Make the samples warmer and keep dress highlights softer.") ||
      !revised_sample_batch.value(QStringLiteral("feedbackMemoryDeferred")).toBool() ||
      !revised_sample_batch.value(QStringLiteral("sampleOnlyDraft")).toBool() ||
      revised_sample_batch.value(QStringLiteral("draftedCount")).toInt() != 2 ||
      revised_third_state.exposure != third_original_state.exposure ||
      revised_sample_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("key")).toString() !=
          QStringLiteral("apply_to_all") ||
      !controller.draftActive()) {
    return fail("sample review feedback should immediately revise samples without touching remaining photos or durable style memory");
  }
  const auto unconfirmed_apply_all = controller.applySampleReviewToAll();
  if (unconfirmed_apply_all.value(QStringLiteral("ok")).toBool() ||
      !unconfirmed_apply_all.value(QStringLiteral("confirmationRequired")).toBool() ||
      !unconfirmed_apply_all.value(QStringLiteral("sampleOnlyDraft")).toBool() ||
      unconfirmed_apply_all.value(QStringLiteral("sampleRevisionFeedback")).toString() !=
          QStringLiteral("Make the samples warmer and keep dress highlights softer.") ||
      !unconfirmed_apply_all.value(QStringLiteral("feedbackMemoryDeferred")).toBool() ||
      unconfirmed_apply_all.value(QStringLiteral("remainingAssetIds")).toStringList() !=
          QStringList{QStringLiteral("asset-3")} ||
      !controller.draftActive()) {
    return fail("sample review Apply to All should require confirmation while preserving draft context");
  }
  const auto confirmed_apply_all = controller.applySampleReviewToAll(true);
  const auto approved_feedback_memory = confirmed_apply_all.value(QStringLiteral("approvedFeedbackMemory")).toMap();
  const auto approved_feedback_style =
      read_json_object(approved_feedback_memory.value(QStringLiteral("stylePath")).toString());
  const auto approved_feedback_note =
      QString::fromUtf8(read_file_bytes(confirmed_apply_all.value(QStringLiteral("approvedFeedbackMemoryNotePath")).toString()));
  const auto confirmed_third_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(2).value(QStringLiteral("recipeJson")).toString());
  if (!confirmed_apply_all.value(QStringLiteral("ok")).toBool() ||
      !confirmed_apply_all.value(QStringLiteral("sampleOnlyContinuation")).toBool() ||
      !confirmed_apply_all.value(QStringLiteral("sampleFeedbackApproved")).toBool() ||
      confirmed_apply_all.value(QStringLiteral("feedbackMemoryDeferred")).toBool() ||
      confirmed_apply_all.value(QStringLiteral("approvedFeedback")).toString() !=
          QStringLiteral("Make the samples warmer and keep dress highlights softer.") ||
      confirmed_apply_all.value(QStringLiteral("styleChangeSummary")).toString() !=
          QStringLiteral("Style changed because: Make the samples warmer and keep dress highlights softer.") ||
      approved_feedback_memory.value(QStringLiteral("mode")).toString() != QStringLiteral("style-feedback") ||
      approved_feedback_memory.value(QStringLiteral("styleChangeSummary")).toString() !=
          confirmed_apply_all.value(QStringLiteral("styleChangeSummary")).toString() ||
      approved_feedback_style.value(QStringLiteral("learned_profile"))
              .toObject()
              .value(QStringLiteral("latestFeedback"))
              .toString() != QStringLiteral("Make the samples warmer and keep dress highlights softer.") ||
      !approved_feedback_note.contains(QStringLiteral("Make the samples warmer and keep dress highlights softer.")) ||
      confirmed_apply_all.value(QStringLiteral("userVisibleMemoryEvents")).toList().isEmpty() ||
      confirmed_apply_all.value(QStringLiteral("draftedCount")).toInt() != 1 ||
      confirmed_apply_all.value(QStringLiteral("remainingCount")).toInt() != 0 ||
      confirmed_apply_all.value(QStringLiteral("fullTargetCount")).toInt() != 3 ||
      confirmed_apply_all.value(QStringLiteral("batchSummary"))
              .toMap()
              .value(QStringLiteral("reviewSummary"))
              .toMap()
              .value(QStringLiteral("editedCount"))
              .toInt() != 1 ||
      confirmed_apply_all.value(QStringLiteral("batchSummary"))
              .toMap()
              .value(QStringLiteral("reviewSummary"))
              .toMap()
              .value(QStringLiteral("failedCount"))
              .toInt() != 0 ||
      confirmed_apply_all.value(QStringLiteral("batchSummary"))
              .toMap()
              .value(QStringLiteral("reviewSummary"))
              .toMap()
              .value(QStringLiteral("issueCount"))
              .toInt() !=
          confirmed_apply_all.value(QStringLiteral("batchSummary"))
              .toMap()
              .value(QStringLiteral("reviewIssues"))
              .toList()
              .size() ||
      confirmed_third_state.exposure <= third_original_state.exposure ||
      !controller.draftActive()) {
    return fail("confirmed sample review Apply to All should draft the remaining selected photos");
  }
  const auto post_apply_all_ai_state = controller.aiEditWorkflowState();
  if (post_apply_all_ai_state.value(QStringLiteral("statusLine")).toString() != QStringLiteral("Ready to commit") ||
      post_apply_all_ai_state.value(QStringLiteral("sampleReviewWaiting")).toBool() ||
      post_apply_all_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("key")).toString() !=
          QStringLiteral("commit")) {
    return fail("AI edit workflow state should return to commit after Apply to All drafts remaining photos");
  }
  const auto sample_first_rollback = controller.rollbackDraft();
  if (!sample_first_rollback.value(QStringLiteral("ok")).toBool() ||
      sample_first_rollback.value(QStringLiteral("restoredCount")).toInt() != 3 ||
      controller.draftActive()) {
    return fail("sample-first batch rollback should restore both sample and apply-to-all drafts");
  }
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.selectAll();

  catalog_model.addAssetToProject(QStringLiteral("asset-1"), QStringLiteral("Agent Shoot"));
  catalog_model.addAssetToProject(QStringLiteral("asset-2"), QStringLiteral("Agent Shoot"));
  catalog_model.openProject(QStringLiteral("Agent Shoot"));
  const auto project_validation = controller.validateBatchForScope(QStringLiteral("project"));
  if (!project_validation.value(QStringLiteral("ok")).toBool() ||
      project_validation.value(QStringLiteral("mode")).toString() != QStringLiteral("validate-batch") ||
      project_validation.value(QStringLiteral("scope")).toString() != QStringLiteral("project") ||
      project_validation.value(QStringLiteral("checkedCount")).toInt() != 2 ||
      project_validation.value(QStringLiteral("assetIds")).toStringList().size() != 2 ||
      project_validation.value(QStringLiteral("batchSummary")).toMap().value(QStringLiteral("assetCount")).toInt() != 2 ||
      project_validation.value(QStringLiteral("assetReports")).toList().size() != 2 ||
      controller.draftActive()) {
    return fail("project scope validation should cover the current album/project without creating a draft");
  }

  const auto project_preflight =
      controller.exportPreflightForScope(QStringLiteral("project"), QStringLiteral("Web Proof"), export_dir, {});
  if (!project_preflight.value(QStringLiteral("ok")).toBool() ||
      project_preflight.value(QStringLiteral("mode")).toString() != QStringLiteral("export-preflight") ||
      project_preflight.value(QStringLiteral("scope")).toString() != QStringLiteral("project") ||
      project_preflight.value(QStringLiteral("checkedCount")).toInt() != 2 ||
      project_preflight.value(QStringLiteral("assetReports")).toList().size() != 2 ||
      !QFileInfo(project_preflight.value(QStringLiteral("preflightReportPath")).toString()).isFile() ||
      !QFileInfo(project_preflight.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.draftActive()) {
    return fail("project scope export preflight should inspect every project asset without creating a draft");
  }

  catalog_model.setProjectFilter(QStringLiteral("all"));
  catalog_model.setSearchFilter(QStringLiteral("source-2"));
  const auto filtered_report = controller.runDraftForScope(
      QStringLiteral("filtered-library"),
      QStringLiteral("apply a filtered one-photo draft"));
  if (!filtered_report.value(QStringLiteral("ok")).toBool() ||
      filtered_report.value(QStringLiteral("scope")).toString() != QStringLiteral("filtered-library") ||
      filtered_report.value(QStringLiteral("draftedCount")).toInt() != 1 ||
      filtered_report.value(QStringLiteral("assetIds")).toStringList() != QStringList{QStringLiteral("asset-2")} ||
      filtered_report.value(QStringLiteral("assetReports")).toList().size() != 1 ||
      !controller.draftActive()) {
    return fail("filtered-library scope draft should apply only the currently visible filtered asset");
  }
  const auto filtered_first_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetsForProject(QStringLiteral("all"))
                                                        .at(0)
                                                        .toMap()
                                                        .value(QStringLiteral("recipeJson"))
                                                        .toString());
  const auto filtered_visible_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.visibleAssets()
                                                        .first()
                                                        .toMap()
                                                        .value(QStringLiteral("recipeJson"))
                                                        .toString());
  if (filtered_first_state.exposure != original_state.exposure ||
      filtered_visible_state.exposure <= second_original_state.exposure) {
    return fail("filtered-library draft should leave hidden assets unchanged and update the visible target");
  }
  const auto filtered_rollback = controller.rollbackDraft();
  if (!filtered_rollback.value(QStringLiteral("ok")).toBool() ||
      filtered_rollback.value(QStringLiteral("restoredCount")).toInt() != 1 ||
      controller.draftActive()) {
    return fail("filtered-library rollback should restore the one scoped draft");
  }
  catalog_model.setSearchFilter(QString());
  catalog_model.setProjectFilter(QStringLiteral("all"));
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  catalog_model.toggleSelectionIndex(1);

  const auto second_batch_report = controller.runDraftOnSelected(QStringLiteral("commit the style consistently"));
  if (!second_batch_report.value(QStringLiteral("ok")).toBool() ||
      second_batch_report.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      second_batch_report.value(QStringLiteral("draftedCount")).toInt() != 2 ||
      !controller.draftActive()) {
    return fail("selected-set draft should be ready for commit verification");
  }
  const auto unconfirmed_commit_batch = controller.commitDraft();
  if (unconfirmed_commit_batch.value(QStringLiteral("ok")).toBool() ||
      unconfirmed_commit_batch.value(QStringLiteral("committedCount")).toInt() != 0 ||
      !unconfirmed_commit_batch.value(QStringLiteral("confirmationRequired")).toBool() ||
      !unconfirmed_commit_batch.value(QStringLiteral("draftActive")).toBool() ||
      unconfirmed_commit_batch.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1 ||
      unconfirmed_commit_batch.value(QStringLiteral("assetReports")).toList().size() != 2 ||
      unconfirmed_commit_batch.value(QStringLiteral("batchSummary"))
              .toMap()
              .value(QStringLiteral("statusCounts"))
              .toMap()
              .value(QStringLiteral("drafted"))
              .toInt() != 2 ||
      unconfirmed_commit_batch.value(QStringLiteral("batchTransaction"))
              .toMap()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("awaiting_commit_confirmation") ||
      !unconfirmed_commit_batch.value(QStringLiteral("batchTransaction"))
              .toMap()
              .value(QStringLiteral("rollbackAvailable"))
              .toBool() ||
      !controller.draftActive()) {
    return fail("selected-set commit should require explicit confirmation while preserving the batch summary");
  }
  const auto commit_batch = controller.commitDraft(true);
  if (!commit_batch.value(QStringLiteral("ok")).toBool() ||
      commit_batch.value(QStringLiteral("mode")).toString() != QStringLiteral("commit") ||
      commit_batch.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      commit_batch.value(QStringLiteral("committedCount")).toInt() != 2 ||
      !commit_batch.value(QStringLiteral("confirmed")).toBool() ||
      commit_batch.value(QStringLiteral("assetIds")).toStringList().size() != 2 ||
      commit_batch.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1 ||
      commit_batch.value(QStringLiteral("batchSummary"))
              .toMap()
              .value(QStringLiteral("statusCounts"))
              .toMap()
              .value(QStringLiteral("committed"))
              .toInt() != 2 ||
      commit_batch.value(QStringLiteral("batchTransaction"))
              .toMap()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("committed") ||
      commit_batch.value(QStringLiteral("batchTransaction"))
              .toMap()
              .value(QStringLiteral("rollbackAvailable"))
              .toBool() ||
      commit_batch.value(QStringLiteral("draftActive")).toBool() ||
      controller.draftActive()) {
    return fail("selected-set commit should report and clear a two-asset draft transaction");
  }
  const auto committed_first_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString());
  const auto committed_second_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(1).value(QStringLiteral("recipeJson")).toString());
  if (committed_first_state.exposure <= original_state.exposure ||
      committed_second_state.exposure <= second_original_state.exposure) {
    return fail("selected-set commit should preserve drafted recipe changes");
  }
  run_history = controller.runHistory();
  if (run_history.size() < 5 ||
      run_history.first().toMap().value(QStringLiteral("mode")).toString() != QStringLiteral("commit") ||
      run_history.first().toMap().value(QStringLiteral("assetCount")).toInt() != 2 ||
      run_history.size() > 20) {
    return fail("agent run history should retain recent user-facing run summaries newest-first");
  }
  const auto empty_commit = controller.commitDraft(true);
  if (empty_commit.value(QStringLiteral("ok")).toBool() ||
      empty_commit.value(QStringLiteral("committedCount")).toInt() != 0 ||
      controller.draftActive()) {
    return fail("commit without an active draft should fail without mutating state");
  }

  const auto source_recipe_before_virtual_copy =
      catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString();
  const auto virtual_copy_id =
      catalog_model.createVirtualCopyForAsset(QStringLiteral("asset-1"), QStringLiteral("Agent Variant"));
  if (virtual_copy_id.isEmpty()) {
    return fail("agent virtual-copy independence test should be able to create a variant");
  }
  auto virtual_copy_row = -1;
  for (auto row = 0; row < catalog_model.assetCount(); ++row) {
    if (catalog_model.assetAt(row).value(QStringLiteral("assetId")).toString() == virtual_copy_id) {
      virtual_copy_row = row;
      break;
    }
  }
  if (virtual_copy_row < 0) {
    return fail("agent virtual-copy variant should be visible in the catalog");
  }
  catalog_model.selectIndex(virtual_copy_row);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(virtual_copy_row);
  const auto virtual_copy_report =
      controller.runDraftForScope(QStringLiteral("selected-assets"), QStringLiteral("edit the virtual copy only"));
  const auto virtual_copy_asset_reports = virtual_copy_report.value(QStringLiteral("assetReports")).toList();
  const auto virtual_copy_batch_summary = virtual_copy_report.value(QStringLiteral("batchSummary")).toMap();
  const auto variant_families = virtual_copy_batch_summary.value(QStringLiteral("variantFamilies")).toList();
  if (!virtual_copy_report.value(QStringLiteral("ok")).toBool() ||
      virtual_copy_report.value(QStringLiteral("draftedCount")).toInt() != 1 ||
      virtual_copy_asset_reports.size() != 1 ||
      !virtual_copy_asset_reports.first().toMap().value(QStringLiteral("virtualCopy")).toBool() ||
      virtual_copy_asset_reports.first().toMap().value(QStringLiteral("sourceAssetId")).toString() !=
          QStringLiteral("asset-1") ||
      virtual_copy_asset_reports.first().toMap().value(QStringLiteral("virtualCopyPolicy")).toString() !=
          QStringLiteral("target-asset-id-only") ||
      virtual_copy_batch_summary.value(QStringLiteral("virtualCopyPolicy")).toString() !=
          QStringLiteral("target-asset-id-only") ||
      variant_families.size() != 1 ||
      variant_families.first().toMap().value(QStringLiteral("sourceAssetId")).toString() !=
          QStringLiteral("asset-1") ||
      variant_families.first().toMap().value(QStringLiteral("virtualCopyAssetIds")).toStringList() !=
          QStringList{virtual_copy_id} ||
      catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString() !=
          source_recipe_before_virtual_copy ||
      !controller.draftActive()) {
    return fail("agent virtual-copy draft should report variant metadata and leave the source recipe untouched");
  }
  if (catalog_model.assetAt(virtual_copy_row).value(QStringLiteral("recipeJson")).toString() ==
      source_recipe_before_virtual_copy) {
    return fail("agent virtual-copy draft should edit the variant recipe");
  }
  const auto virtual_copy_rollback = controller.rollbackDraft();
  if (!virtual_copy_rollback.value(QStringLiteral("ok")).toBool() ||
      virtual_copy_rollback.value(QStringLiteral("restoredCount")).toInt() != 1 ||
      catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString() !=
          source_recipe_before_virtual_copy ||
      controller.draftActive()) {
    return fail("agent virtual-copy rollback should restore only the drafted variant");
  }
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  catalog_model.toggleSelectionIndex(1);

  const auto first_recipe_before_cancel =
      catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString();
  const auto second_recipe_before_cancel =
      catalog_model.assetAt(1).value(QStringLiteral("recipeJson")).toString();
  QTimer::singleShot(0, [&controller]() {
    controller.cancelRun(QStringLiteral("user_stop"));
  });
  const auto user_canceled_draft = controller.applyDraftRecipePatchToSelected(
      QVariantMap{
          {QStringLiteral("globalTone"),
           QVariantMap{{QStringLiteral("exposure"), 0.2}}},
      });
  if (user_canceled_draft.value(QStringLiteral("ok")).toBool() ||
      user_canceled_draft.value(QStringLiteral("cancellationReason")).toString() !=
          QStringLiteral("user_stop") ||
      !user_canceled_draft.value(QStringLiteral("rollbackOnCancel")).toBool() ||
      user_canceled_draft.value(QStringLiteral("partialDraftRollback")).toString() !=
          QStringLiteral("nothing_applied") ||
      user_canceled_draft.value(QStringLiteral("appliedCount")).toInt() != 0 ||
      user_canceled_draft.value(QStringLiteral("restoredCount")).toInt() != 0 ||
      user_canceled_draft.value(QStringLiteral("draftActive")).toBool() ||
      controller.draftActive() ||
      controller.cancellationRequested() ||
      catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString() !=
          first_recipe_before_cancel ||
      catalog_model.assetAt(1).value(QStringLiteral("recipeJson")).toString() !=
          second_recipe_before_cancel ||
      !QFileInfo(user_canceled_draft.value(QStringLiteral("manifestPath")).toString()).isFile()) {
    return fail("user-canceled agent draft should stop and report rollback of any partial draft writes");
  }
  const auto user_canceled_manifest =
      read_json_object(user_canceled_draft.value(QStringLiteral("manifestPath")).toString());
  if (user_canceled_manifest.value(QStringLiteral("cancellationReason")).toString() !=
          QStringLiteral("user_stop") ||
      !user_canceled_manifest.value(QStringLiteral("rollbackOnCancel")).toBool() ||
      user_canceled_manifest.value(QStringLiteral("partialDraftRollback")).toString() !=
          QStringLiteral("nothing_applied") ||
      user_canceled_manifest.value(QStringLiteral("batchTransaction"))
              .toObject()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("canceled_rolled_back")) {
    return fail("user-canceled draft manifest should persist rollback-on-cancel transaction metadata");
  }

  controller.setRunTimeoutMs(0);
  const auto timed_out_draft = controller.applyDraftRecipePatchToSelected(
      QVariantMap{
          {QStringLiteral("globalTone"),
           QVariantMap{{QStringLiteral("exposure"), 0.2}}},
      });
  if (timed_out_draft.value(QStringLiteral("ok")).toBool() ||
      timed_out_draft.value(QStringLiteral("cancellationReason")).toString() != QStringLiteral("timeout") ||
      timed_out_draft.value(QStringLiteral("failureReason")).toString().isEmpty() ||
      !QFileInfo(timed_out_draft.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.draftActive() ||
      controller.cancellationRequested()) {
    return fail("agent draft timeout should record cancellation metadata without leaving an active draft");
  }
  const auto timed_out_manifest =
      read_json_object(timed_out_draft.value(QStringLiteral("manifestPath")).toString());
  if (timed_out_manifest.value(QStringLiteral("cancellationReason")).toString() != QStringLiteral("timeout") ||
      timed_out_manifest.value(QStringLiteral("failureReason")).toString().isEmpty() ||
      timed_out_manifest.value(QStringLiteral("durationMs")).toInt(-1) < 0) {
    return fail("timed-out draft manifest should persist cancellation reason, failure reason, and timing");
  }
  const auto timed_out_batch_validation = controller.validateBatchForSelected();
  if (timed_out_batch_validation.value(QStringLiteral("ok")).toBool() ||
      timed_out_batch_validation.value(QStringLiteral("cancellationReason")).toString() != QStringLiteral("timeout") ||
      timed_out_batch_validation.value(QStringLiteral("checkedCount"), -1).toInt() != 0 ||
      !QFileInfo(timed_out_batch_validation.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.cancellationRequested()) {
    return fail("batch validation timeout should stop before processing assets and persist cancellation metadata");
  }
  const auto timed_out_batch_manifest =
      read_json_object(timed_out_batch_validation.value(QStringLiteral("manifestPath")).toString());
  if (timed_out_batch_manifest.value(QStringLiteral("cancellationReason")).toString() != QStringLiteral("timeout") ||
      timed_out_batch_manifest.value(QStringLiteral("failureReason")).toString().isEmpty() ||
      timed_out_batch_manifest.value(QStringLiteral("durationMs")).toInt(-1) < 0) {
    return fail("timed-out batch validation manifest should persist cancellation reason, failure reason, and timing");
  }
  const auto timed_out_preflight =
      controller.exportPreflightForSelected(QString(), QString(), {});
  if (timed_out_preflight.value(QStringLiteral("ok")).toBool() ||
      timed_out_preflight.value(QStringLiteral("cancellationReason")).toString() != QStringLiteral("timeout") ||
      timed_out_preflight.value(QStringLiteral("checkedCount"), -1).toInt() != 0 ||
      !QFileInfo(timed_out_preflight.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.cancellationRequested()) {
    return fail("export preflight timeout should stop before processing assets and persist cancellation metadata");
  }
  const auto timed_out_preflight_manifest =
      read_json_object(timed_out_preflight.value(QStringLiteral("manifestPath")).toString());
  if (timed_out_preflight_manifest.value(QStringLiteral("cancellationReason")).toString() != QStringLiteral("timeout") ||
      timed_out_preflight_manifest.value(QStringLiteral("failureReason")).toString().isEmpty() ||
      timed_out_preflight_manifest.value(QStringLiteral("durationMs")).toInt(-1) < 0) {
    return fail("timed-out export preflight manifest should persist cancellation reason, failure reason, and timing");
  }
  controller.setRunTimeoutMs(300000);

  catalog_model.replaceAssetsForTesting({
      make_asset(
          QStringLiteral("asset-missing"),
          QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/missing.png")),
          original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.toggleSelectionIndex(0);
  const auto missing_source_validation = controller.validateBatchForSelected();
  if (missing_source_validation.value(QStringLiteral("ok")).toBool() ||
      missing_source_validation.value(QStringLiteral("failureReason")).toString().isEmpty() ||
      !QFileInfo(missing_source_validation.value(QStringLiteral("manifestPath")).toString()).isFile()) {
    return fail("missing source batch validation should fail with persisted failure metadata");
  }
  const auto missing_source_manifest =
      read_json_object(missing_source_validation.value(QStringLiteral("manifestPath")).toString());
  if (missing_source_manifest.value(QStringLiteral("failureReason")).toString().isEmpty() ||
      missing_source_manifest.value(QStringLiteral("errors")).toArray().isEmpty() ||
      missing_source_manifest.value(QStringLiteral("durationMs")).toInt(-1) < 0) {
    return fail("failed batch validation manifest should record failure reason, errors, and timing");
  }

  if (observability_log_path.isEmpty() ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.run_started")) < 4 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.tool_call")) < 4 ||
      observability_event_count(
          observability_log_path,
          QStringLiteral("agent.edit.proof_artifacts_written")) < 1 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.validation_failed")) < 1 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.draft_applied")) < 1 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.commit")) < 1 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.rollback")) < 1 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.export_preflight")) < 1 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.export_queued")) < 1) {
    return fail("agent controller should write observability events for runs, tools, validation, proof, draft, commit, rollback, and export");
  }

  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  controller.setSelectedStyleId(QStringLiteral("moody-test-style-2"));
  controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));
  const QJsonObject direct_apply_patch_content{
      {QStringLiteral("decision"), QStringLiteral("recipe_patch")},
      {QStringLiteral("summary"), QStringLiteral("Clean balanced edit for the current photo.")},
      {QStringLiteral("styleConstraintCitations"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("field"), QStringLiteral("style.goal")},
               {QStringLiteral("reason"), QStringLiteral("The selected style asks for a polished natural edit.")},
           },
       }},
      {QStringLiteral("recipePatch"),
       QJsonObject{
           {QStringLiteral("globalTone"),
            QJsonObject{
                {QStringLiteral("exposure"), 0.12},
                {QStringLiteral("contrast"), 8.0},
            }},
           {QStringLiteral("color"),
            QJsonObject{
                {QStringLiteral("saturation"), 5.0},
            }},
       }},
  };
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON", fake_ollama_response(direct_apply_patch_content));
  const auto direct_apply_report =
      controller.applyEditForScope(QStringLiteral("current-photo"), QStringLiteral("make it look finished"));
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  if (!direct_apply_report.value(QStringLiteral("ok")).toBool() ||
      direct_apply_report.value(QStringLiteral("mode")).toString() != QStringLiteral("apply-edit") ||
      !direct_apply_report.value(QStringLiteral("committed")).toBool() ||
      direct_apply_report.value(QStringLiteral("committedCount")).toInt() != 1 ||
      direct_apply_report.value(QStringLiteral("draftActive")).toBool() ||
      controller.draftActive() ||
      controller.status() != QStringLiteral("Applied")) {
    return fail("primary agent apply should plan, apply, and commit a normal single-photo edit without leaving a hidden draft");
  }

  if (read_file_bytes(source_path) != original_source_bytes ||
      read_file_bytes(second_source_path) != original_second_source_bytes ||
      read_file_bytes(third_source_path) != original_third_source_bytes) {
    return fail("agent workflows must not overwrite source image files");
  }
  const auto memory_root_path = controller.memoryVaultStatus().value(QStringLiteral("rootPath")).toString();
  const auto legacy_memory_note_path =
      QDir(memory_root_path).filePath(QStringLiteral("feedback/legacy-unversioned-note.md"));
  if (!QDir().mkpath(QFileInfo(legacy_memory_note_path).dir().absolutePath())) {
    return fail("test should be able to create a legacy memory note for schema migration checks");
  }
  QFile legacy_memory_note(legacy_memory_note_path);
  if (!legacy_memory_note.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
      legacy_memory_note.write("---\ntype: agent_feedback_memory\n---\n# Legacy note\n") <= 0) {
    return fail("test should be able to write a legacy memory note for schema migration checks");
  }
  legacy_memory_note.close();
  const auto legacy_vault_status = controller.memoryVaultStatus();
  if (legacy_vault_status.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
      legacy_vault_status.value(QStringLiteral("schemaVersionOk")).toBool() ||
      !legacy_vault_status.value(QStringLiteral("migrationRequired")).toBool() ||
      legacy_vault_status.value(QStringLiteral("schemaMismatchCount")).toInt() < 1 ||
      legacy_vault_status.value(QStringLiteral("schemaMismatches")).toList().isEmpty()) {
    return fail("memory vault status should detect legacy or unversioned notes that require migration");
  }
  const auto confirmed_memory_reset = controller.resetMemoryVault(true);
  const auto reset_vault_status = controller.memoryVaultStatus();
  if (!confirmed_memory_reset.value(QStringLiteral("ok")).toBool() ||
      confirmed_memory_reset.value(QStringLiteral("removedFileCount")).toInt() <= 0 ||
      reset_vault_status.value(QStringLiteral("exists")).toBool() ||
      reset_vault_status.value(QStringLiteral("totalNoteCount")).toInt() != 0) {
    return fail("confirmed memory vault reset should remove local memory notes and refresh vault status");
  }

  return EXIT_SUCCESS;
}
