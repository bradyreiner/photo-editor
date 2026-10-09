#include "app_bootstrap.h"
#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_provider.h"
#include "agent_mcp_client.h"
#include "agent_edit_style.h"
#include "agent_local_model.h"
#include "agent_edit_controller.h"
#include "agent_photo_commands.h"
#include "catalog_asset_model.h"
#include "photo_render_utils.h"
#include "photo_automation_bridge.h"
#include "photo_mcp_server.h"
#include "photo_text_rasterizer.h"
#include <QGuiApplication>
#include <QFont>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QSaveFile>
#include <QThread>
#include <QDir>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static EditState state(const EditorPreviewController& c) { return c.currentState(); }
  static int pending_brush_commit_count(const EditorPreviewController& c) {
    return static_cast<int>(c.pending_brush_commits_.size()) +
           (c.pending_brush_commit_scheduled_ ? 1 : 0) +
           (c.mask_worker_coordinator_.brushRasterCommitWatcher().isRunning() ? 1 : 0) +
           (c.brush_raster_commit_active_ ? 1 : 0);
  }
  static void clearHistory(EditorPreviewController& c, const QString& asset_id) {
    c.clearHistoryForAsset(asset_id);
  }
};
}
using namespace photo_editor::desktop;
#define CHECK(c, m) do { if (!(c)) { std::cerr << m << '\n'; return 1; } } while (false)
int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QVariantMap normalize_live_composition_tool_call(const QVariant& value) {
  const auto model_call = value.toMap();
  if (model_call.contains(QStringLiteral("name")) &&
      model_call.contains(QStringLiteral("arguments"))) {
    if (model_call.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        model_call.value(QStringLiteral("arguments")).metaType().id() != QMetaType::QVariantMap)
      return {};
    auto arguments = model_call.value(QStringLiteral("arguments")).toMap();
    if (!arguments.contains(QStringLiteral("assetId")))
      arguments.insert(QStringLiteral("assetId"), QStringLiteral("composition"));
    return {{QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
        {QStringLiteral("arguments"), arguments},
        {QStringLiteral("modelOutput"), model_call}};
  }
  static const QSet<QString> argument_keys{
      QStringLiteral("assetId"), QStringLiteral("operation"),
      QStringLiteral("layerId"), QStringLiteral("values")};
  for (auto it = model_call.cbegin(); it != model_call.cend(); ++it)
    if (!argument_keys.contains(it.key())) return {};
  if (!model_call.contains(QStringLiteral("operation")) ||
      !model_call.contains(QStringLiteral("values")) ||
      model_call.value(QStringLiteral("values")).metaType().id() != QMetaType::QVariantMap)
    return {};
  auto arguments = model_call;
  if (!arguments.contains(QStringLiteral("assetId")))
    arguments.insert(QStringLiteral("assetId"), QStringLiteral("composition"));
  return {{QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
      {QStringLiteral("arguments"), arguments},
      {QStringLiteral("modelOutput"), model_call}};
}

namespace {
class ControllerMcpEndpoint final : public QObject {
 public:
  explicit ControllerMcpEndpoint(EditorPreviewController* controller)
      : controller_(controller) {
    QObject::connect(&server_, &QTcpServer::newConnection, this, [this]() {
      while (server_.hasPendingConnections()) {
        auto* socket = server_.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
          auto buffer = socket->property("requestBuffer").toByteArray();
          buffer.append(socket->readAll());
          const auto header_end = buffer.indexOf("\r\n\r\n");
          if (header_end < 0) {
            socket->setProperty("requestBuffer", buffer);
            return;
          }
          auto content_length = -1;
          const auto header = buffer.left(header_end);
          for (const auto& line_bytes : header.split('\n')) {
            const auto line = QString::fromLatin1(line_bytes).trimmed();
            if (line.toLower().startsWith(QStringLiteral("content-length:"))) {
              bool ok = false;
              content_length = line.mid(QStringLiteral("content-length:").size()).trimmed().toInt(&ok);
              if (!ok) content_length = -1;
            }
          }
          const auto body_start = header_end + 4;
          if (content_length < 0 || buffer.size() - body_start < content_length) {
            socket->setProperty("requestBuffer", buffer);
            return;
          }
          const auto request = QJsonDocument::fromJson(buffer.mid(body_start, content_length)).object();
          const auto method = request.value(QStringLiteral("method")).toString();
          const auto id = request.value(QStringLiteral("id"));
          QJsonObject result;
          if (method == QStringLiteral("tools/list")) {
            QJsonObject value_properties;
            value_properties.insert(QStringLiteral("text"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
            value_properties.insert(QStringLiteral("x"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}});
            value_properties.insert(QStringLiteral("y"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}});
            value_properties.insert(QStringLiteral("maskId"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
            value_properties.insert(QStringLiteral("independent"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}});
            value_properties.insert(QStringLiteral("size"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}});
            value_properties.insert(QStringLiteral("color"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
            value_properties.insert(QStringLiteral("strokeWidth"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}});
            value_properties.insert(QStringLiteral("strokeColor"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
            value_properties.insert(QStringLiteral("effectType"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("string")},
                {QStringLiteral("enum"), QJsonArray{QStringLiteral("gaussian_blur"),
                    QStringLiteral("box_blur"), QStringLiteral("motion_blur"),
                    QStringLiteral("posterize_linear_rgb"), QStringLiteral("invert_linear_rgb"),
                    QStringLiteral("unsharp_mask"), QStringLiteral("optical_diffusion"),
                    QStringLiteral("imperfect_print"), QStringLiteral("dragged_shutter")}}});
            for (const auto* field : {"radius", "amount", "strength", "threshold",
                    "highlightThreshold", "highlightKnee", "warmth", "highlightProtection",
                    "sigma", "distance", "angle", "levels", "paperScale", "paperTexture",
                    "inkSpread", "inkDensity", "redRegistrationX", "redRegistrationY",
                    "blueRegistrationX", "blueRegistrationY", "seed", "falloff",
                    "highlightWeighting"})
              value_properties.insert(QString::fromLatin1(field),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}});
            value_properties.insert(QStringLiteral("enabled"),
                QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}});
            value_properties.insert(QStringLiteral("protectSubject"),
                QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}});
            QJsonObject values_schema{{QStringLiteral("type"), QStringLiteral("object")}};
            values_schema.insert(QStringLiteral("properties"), value_properties);
            QJsonObject operation_schema{{QStringLiteral("type"), QStringLiteral("string")}};
            operation_schema.insert(QStringLiteral("enum"), QJsonArray{
                QStringLiteral("inspect"), QStringLiteral("add_text"), QStringLiteral("add_pixel_layer"),
                QStringLiteral("add_source_layer"),
                QStringLiteral("typography"), QStringLiteral("copy_selection")});
            auto operations = operation_schema.value(QStringLiteral("enum")).toArray();
            operations.append(QStringLiteral("add_effect"));
            operation_schema.insert(QStringLiteral("enum"), operations);
            QJsonObject command_properties;
            command_properties.insert(QStringLiteral("assetId"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
            command_properties.insert(QStringLiteral("operation"), operation_schema);
            command_properties.insert(QStringLiteral("layerId"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
            command_properties.insert(QStringLiteral("values"), values_schema);
            QJsonObject command_schema{{QStringLiteral("type"), QStringLiteral("object")}};
            command_schema.insert(QStringLiteral("properties"), command_properties);
            command_schema.insert(QStringLiteral("required"), QJsonArray{
                QStringLiteral("assetId"), QStringLiteral("operation")});
            result.insert(QStringLiteral("tools"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("name"), QStringLiteral("photo.inspect_composition")},
                    {QStringLiteral("description"), QStringLiteral("Inspect editable layers, masks, and canvas dimensions.")},
                    {QStringLiteral("inputSchema"), QJsonObject{
                        {QStringLiteral("type"), QStringLiteral("object")},
                        {QStringLiteral("properties"), QJsonObject{{QStringLiteral("assetId"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                        {QStringLiteral("required"), QJsonArray{QStringLiteral("assetId")}}}}},
                QJsonObject{
                    {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
                    {QStringLiteral("description"), QStringLiteral("Create and style editable text, then duplicate selected pixels above it. Values are operation-specific.")},
                    {QStringLiteral("inputSchema"), command_schema}}});
          } else if (method == QStringLiteral("tools/call")) {
            const auto params = request.value(QStringLiteral("params")).toObject();
            const auto name = params.value(QStringLiteral("name")).toString();
            const auto arguments = params.value(QStringLiteral("arguments")).toObject();
            QVariantMap command_result;
            if (name == QStringLiteral("photo.edit_composition") ||
                name == QStringLiteral("photo.inspect_composition")) {
              auto command = arguments.toVariantMap();
              if (name == QStringLiteral("photo.inspect_composition"))
                command.insert(QStringLiteral("operation"), QStringLiteral("inspect"));
              command_result = controller_->photoCompositionCommand(command);
            } else {
              command_result = {{QStringLiteral("ok"), false},
                                {QStringLiteral("error"), QStringLiteral("Unknown test tool.")}};
            }
            const auto ok = command_result.value(QStringLiteral("ok")).toBool();
            result.insert(QStringLiteral("isError"), !ok);
            result.insert(QStringLiteral("structuredContent"), QJsonObject::fromVariantMap(command_result));
            result.insert(QStringLiteral("content"), QJsonArray{QJsonObject{
                {QStringLiteral("type"), QStringLiteral("text")},
                {QStringLiteral("text"), ok ? QStringLiteral("Composition updated.")
                                             : command_result.value(QStringLiteral("error")).toString()}}});
          }
          const auto response = QJsonDocument(QJsonObject{
              {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
              {QStringLiteral("id"), id},
              {QStringLiteral("result"), result}}).toJson(QJsonDocument::Compact);
          QByteArray http;
          http.append("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: ");
          http.append(QByteArray::number(response.size()));
          http.append("\r\n\r\n");
          http.append(response);
          socket->write(http);
          socket->flush();
          socket->disconnectFromHost();
        });
      }
    });
  }

  bool start() { return server_.listen(QHostAddress::LocalHost, 0); }
  QString errorString() const { return server_.errorString(); }
  QString endpoint() const {
    return QStringLiteral("http://127.0.0.1:%1/mcp").arg(server_.serverPort());
  }

 private:
  EditorPreviewController* controller_ = nullptr;
  QTcpServer server_;
};

class LiveCreativeRunJournal final {
 public:
  explicit LiveCreativeRunJournal(QString path) : path_(std::move(path)) {}
  LiveCreativeRunJournal(const LiveCreativeRunJournal&) = delete;
  LiveCreativeRunJournal& operator=(const LiveCreativeRunJournal&) = delete;
  ~LiveCreativeRunJournal() {
    if (!complete_) {
      data_.insert(QStringLiteral("status"), QStringLiteral("failed_or_interrupted"));
      static_cast<void>(write());
    }
  }

  bool record(QString phase, const QVariantMap& details = {}) {
    data_.insert(QStringLiteral("phase"), std::move(phase));
    for (auto it = details.cbegin(); it != details.cend(); ++it) data_.insert(it.key(), it.value());
    return write();
  }

  bool finish(const QString& receipt_path) {
    data_.insert(QStringLiteral("phase"), QStringLiteral("complete"));
    data_.insert(QStringLiteral("status"), QStringLiteral("passed"));
    data_.insert(QStringLiteral("receiptPath"), receipt_path);
    if (!write()) return false;
    complete_ = true;
    return true;
  }

  bool finishWithStatus(QString status, const QVariantMap& details = {}) {
    data_.insert(QStringLiteral("status"), std::move(status));
    for (auto it = details.cbegin(); it != details.cend(); ++it) data_.insert(it.key(), it.value());
    if (!write()) return false;
    complete_ = true;
    return true;
  }

 private:
  bool write() const {
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto bytes = QJsonDocument::fromVariant(data_).toJson(QJsonDocument::Indented);
    return file.write(bytes) == bytes.size() && file.commit();
  }

  QString path_;
  QVariantMap data_;
  bool complete_ = false;
};

QVariantMap currentCreativeRunSourceEvidence() {
  const auto source_root = QStringLiteral(PHOTO_EDITOR_SOURCE_DIR);
  QProcess revision_process;
  revision_process.setWorkingDirectory(source_root);
  revision_process.start(QStringLiteral("git"), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
  if (!revision_process.waitForStarted(3000) || !revision_process.waitForFinished(3000) ||
      revision_process.exitStatus() != QProcess::NormalExit || revision_process.exitCode() != 0)
    return {};
  const auto revision = QString::fromUtf8(revision_process.readAllStandardOutput()).trimmed();
  if (revision.isEmpty()) return {};

  const QStringList relative_paths{
      QStringLiteral("tests/CMakeLists.txt"),
      QStringLiteral("tests/photo_composition_workflow_test.cpp"),
      QStringLiteral("docs/creative-tools-plan.md"),
      QStringLiteral("tests/fixtures/creative-tools/live-workflow-source-notes.md")};
  QVariantList files;
  for (const auto& relative_path : relative_paths) {
    QFile file(QDir(source_root).filePath(relative_path));
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto bytes = file.readAll();
    files.append(QVariantMap{
        {QStringLiteral("file"), relative_path},
        {QStringLiteral("sha256"), QString::fromLatin1(
             QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
        {QStringLiteral("kind"), QStringLiteral("current-source-file")}});
  }

  QProcess diff_process;
  diff_process.setWorkingDirectory(source_root);
  diff_process.start(QStringLiteral("git"), {QStringLiteral("diff"), QStringLiteral("--binary"),
      QStringLiteral("--"), QStringLiteral("tests/CMakeLists.txt")});
  if (!diff_process.waitForStarted(3000) || !diff_process.waitForFinished(5000) ||
      diff_process.exitStatus() != QProcess::NormalExit || diff_process.exitCode() != 0)
    return {};
  const auto diff = diff_process.readAllStandardOutput();
  const auto diff_hash = QString::fromLatin1(
      QCryptographicHash::hash(diff, QCryptographicHash::Sha256).toHex());
  const auto scoped_evidence = QJsonDocument::fromVariant(QVariantMap{
      {QStringLiteral("sourceRevision"), revision},
      {QStringLiteral("workingTreeDiffSha256"), diff_hash},
      {QStringLiteral("files"), files}}).toJson(QJsonDocument::Compact);
  return {{QStringLiteral("sourceRevision"), revision},
      {QStringLiteral("scopedWorkingTreeDiffSha256"), diff_hash},
      {QStringLiteral("scopedWorkingTreeDiffFiles"), relative_paths},
      {QStringLiteral("sourceEvidenceSha256"), QString::fromLatin1(
           QCryptographicHash::hash(scoped_evidence, QCryptographicHash::Sha256).toHex())},
      {QStringLiteral("sourceEvidenceFiles"), files}};
}

int run_production_bridge_live_composition() {
  using namespace photo_editor::desktop;
  const auto live_app_data = qEnvironmentVariable(
      "PHOTO_EDITOR_LIVE_COMPOSITION_APP_DATA_DIR").trimmed();
  const auto model_id = qEnvironmentVariable(
      "PHOTO_EDITOR_LIVE_COMPOSITION_MODEL_ID",
      QStringLiteral("ollama-qwen3-vl-32b")).trimmed();
  const auto receipt_dir = qEnvironmentVariable(
      "PHOTO_EDITOR_LIVE_COMPOSITION_RECEIPT_DIR").trimmed();
  const auto source_path = qEnvironmentVariable("PHOTO_COMPOSITION_REVIEW_SOURCE").trimmed();
  const auto brief_id = qEnvironmentVariable(
      "PHOTO_EDITOR_LIVE_COMPOSITION_BRIEF_ID", QStringLiteral("A1")).trimmed();
  const auto is_a1 = brief_id == QStringLiteral("A1");
  const auto is_a2 = brief_id == QStringLiteral("A2");
  const auto is_a3 = brief_id == QStringLiteral("A3");
  const auto is_a4 = brief_id == QStringLiteral("A4");
  const auto is_a5 = brief_id == QStringLiteral("A5");
  const auto is_a6 = brief_id == QStringLiteral("A6");
  const auto mask_path = qEnvironmentVariable(
      "PHOTO_COMPOSITION_REVIEW_MASK",
      QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/fixtures/creative-tools/sam2-obama-mask.png"))
      .trimmed();
  bool attempt_ok = false;
  const auto attempt = qEnvironmentVariable(
      "PHOTO_EDITOR_LIVE_COMPOSITION_ATTEMPT", QStringLiteral("1")).toInt(&attempt_ok);
  if (live_app_data.isEmpty() || receipt_dir.isEmpty() || source_path.isEmpty())
    return fail("Production bridge run requires app-data, receipt, and real-photo source paths");
  if ((!is_a1 && !is_a2 && !is_a3 && !is_a4 && !is_a5 && !is_a6) || !attempt_ok || attempt < 1 ||
      attempt > (is_a4 ? 99 : 9))
    return fail("This production runner accepts A1-A3 and A5-A6 attempts numbered 1 through 9, and A4 through 99");
  if (!QDir().mkpath(receipt_dir)) return fail("Create the isolated agent-attempt receipt directory");
  const auto status_path = QDir(receipt_dir).filePath(QStringLiteral("run-status.json"));
  const auto receipt_path = QDir(receipt_dir).filePath(QStringLiteral("run-receipt.json"));
  if (QFileInfo::exists(status_path) || QFileInfo::exists(receipt_path) ||
      !QDir(receipt_dir).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty())
    return fail("Refuse to overwrite an existing creative-run attempt receipt");
  LiveCreativeRunJournal journal(status_path);
  if (!journal.record(QStringLiteral("starting"), QVariantMap{
          {QStringLiteral("briefId"), brief_id}, {QStringLiteral("attempt"), attempt},
          {QStringLiteral("modelId"), model_id}, {QStringLiteral("sourcePath"), source_path},
          {QStringLiteral("maskPath"), mask_path}, {QStringLiteral("localOnly"), true},
          {QStringLiteral("cloudUsed"), false}, {QStringLiteral("qualifyingRun"), false}}))
    return fail("Create the append-safe attempt journal before starting the model run");
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", live_app_data.toUtf8());
  if (!AppBootstrap::ensureRuntimeDirectories()) return fail("Prepare the isolated live-agent app-data directory");

  const QImage source(source_path);
  const QImage mask = (is_a1 || is_a4 || is_a6) ? QImage(mask_path) : QImage{};
  if (source.isNull() || ((is_a1 || is_a4 || is_a6) && (mask.isNull() || source.size() != mask.size())))
    return fail(is_a1 ? "Load the retained full-resolution portrait and matching SAM2 selection mask"
                      : is_a4 ? "Load the licensed night-performance photograph and matching performer-protection mask"
                      : is_a6 ? "Load the licensed monochrome photograph and matching contour selection mask"
                      : is_a2 ? "Load the licensed architecture photograph for the print-cover brief"
                              : "Load the licensed glass photograph for the selected creative brief");
  const auto contour_preflight = is_a6 && qEnvironmentVariableIsSet("PHOTO_EDITOR_A6_CONTOUR_PREFLIGHT");
  const auto probe = contour_preflight
      ? QVariantMap{{QStringLiteral("ok"), true}, {QStringLiteral("available"), true},
          {QStringLiteral("modelFamily"), QStringLiteral("diagnostic")}}
      : probe_agent_local_model(model_id);
  if (!probe.value(QStringLiteral("ok")).toBool() ||
      !probe.value(QStringLiteral("available")).toBool()) {
    if (!journal.finishWithStatus(QStringLiteral("preflight_failed"), QVariantMap{
            {QStringLiteral("qualifyingRun"), false},
            {QStringLiteral("modelProbe"), probe}}))
      return fail("Record the failed model preflight before returning");
    std::cerr << "local model probe: "
              << QJsonDocument::fromVariant(probe).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
    return fail("The pinned local vision model must be available for a model-driven run");
  }
  if (!journal.record(QStringLiteral("model_available"), QVariantMap{
          {QStringLiteral("ollamaModel"), probe.value(QStringLiteral("ollamaModel"))},
          {QStringLiteral("modelFamily"), probe.value(QStringLiteral("modelFamily"))}}))
    return fail("Update the attempt journal after model verification");

  const auto gray = (is_a1 || is_a4 || is_a6) ? mask.convertToFormat(QImage::Format_Grayscale8) : QImage{};
  EditRecipeState initial_state;
  if (is_a1 || is_a4 || is_a6) {
    QByteArray alpha(source.width() * source.height(), char(0));
    for (int y = 0; y < source.height(); ++y) {
      const auto* row = gray.constScanLine(y);
      for (int x = 0; x < source.width(); ++x)
        alpha[y * source.width() + x] = static_cast<char>(row[x]);
    }
    initial_state.mask_stack = {QVariantMap{
      {QStringLiteral("id"), is_a1 ? QStringLiteral("sam2-subject") : is_a4 ? QStringLiteral("sam2-lit-performer") : QStringLiteral("sam2-lyskamm-contour")},
      {QStringLiteral("maskSetId"), is_a1 ? QStringLiteral("sam2-subject-selection") : is_a4 ? QStringLiteral("lit-performer-protection") : QStringLiteral("lyskamm-contour-selection")},
      {QStringLiteral("maskSetName"), is_a1 ? QStringLiteral("Prompted subject") : is_a4 ? QStringLiteral("Performer protection") : QStringLiteral("Lyskamm contour selection")},
      {QStringLiteral("toolRole"), QStringLiteral("selection")},
      {QStringLiteral("kind"), QStringLiteral("brush")},
      {QStringLiteral("enabled"), true}, {QStringLiteral("operation"), QStringLiteral("add")},
      {QStringLiteral("segmentationWidth"), source.width()},
      {QStringLiteral("segmentationHeight"), source.height()},
        {QStringLiteral("segmentationData"), alpha}}};
    initial_state.active_mask_index = 0;
  }

  EditorPreviewImageProvider image_provider;
  EditorPreviewController controller;
  controller.setImageProvider(&image_provider);
  controller.loadAsset(QStringLiteral("composition"), source_path, {}, {}, {}, {},
      serialize_edit_recipe_json(initial_state));
  QElapsedTimer load_timer;
  load_timer.start();
  while (controller.displayPreviewSource().isEmpty() && load_timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(5);
  }
  if (controller.displayPreviewSource().isEmpty() ||
      (is_a1 && !controller.selectMaskSet(QStringLiteral("sam2-subject-selection"))) ||
      (is_a4 && !controller.selectMaskSet(QStringLiteral("lit-performer-protection"))) ||
      (is_a6 && !controller.selectMaskSet(QStringLiteral("lyskamm-contour-selection")))) {
    std::cerr << "production controller load diagnostic: "
              << QJsonDocument::fromVariant(QVariantMap{
                     {QStringLiteral("assetId"), controller.assetId()},
                     {QStringLiteral("hasAsset"), controller.hasAsset()},
                     {QStringLiteral("preview"), controller.displayPreviewSource()},
                     {QStringLiteral("width"), controller.sourceImageWidth()},
                     {QStringLiteral("height"), controller.sourceImageHeight()},
                     {QStringLiteral("masks"), controller.maskStack()},
                     {QStringLiteral("status"), controller.status()}})
                     .toJson(QJsonDocument::Compact).toStdString() << '\n';
    return fail("Open the real portrait and its retained subject selection in the editor controller");
  }
  QVariantList a6_outline_paths;
  if (is_a6) a6_outline_paths = controller.activeMaskOutlinePaths();
  if (is_a6 && qEnvironmentVariableIsSet("PHOTO_EDITOR_A6_CONTOUR_PREFLIGHT")) {
    const auto add_text = controller.photoCompositionCommand({
        {QStringLiteral("assetId"), QStringLiteral("composition")},
        {QStringLiteral("operation"), QStringLiteral("add_text")},
        {QStringLiteral("values"), QVariantMap{{QStringLiteral("text"), QStringLiteral("NORTH / 04")},
            {QStringLiteral("x"), 900}, {QStringLiteral("y"), 900}}}});
    const auto layers = controller.photoLayers();
    const auto text_id = layers.isEmpty() ? QString{} : layers.front().toMap().value(QStringLiteral("id")).toString();
    const auto attach = controller.photoCompositionCommand({
        {QStringLiteral("assetId"), QStringLiteral("composition")},
        {QStringLiteral("operation"), QStringLiteral("text_path")},
        {QStringLiteral("layerId"), text_id},
        {QStringLiteral("values"), QVariantMap{{QStringLiteral("maskId"), QStringLiteral("lyskamm-contour-selection")},
            {QStringLiteral("contourIndex"), 0}}}});
    const auto style = controller.photoCompositionCommand({
        {QStringLiteral("assetId"), QStringLiteral("composition")},
        {QStringLiteral("operation"), QStringLiteral("typography")},
        {QStringLiteral("layerId"), text_id},
        {QStringLiteral("values"), QVariantMap{{QStringLiteral("family"), QStringLiteral("Georgia")},
            {QStringLiteral("size"), 58}, {QStringLiteral("weight"), 500},
            {QStringLiteral("color"), QStringLiteral("#fff")}, {QStringLiteral("strokeWidth"), 1},
            {QStringLiteral("strokeColor"), QStringLiteral("#ff202020")},
            {QStringLiteral("pathSide"), QStringLiteral("left")}, {QStringLiteral("pathStart"), 0.08},
            {QStringLiteral("pathEnd"), 0.92}, {QStringLiteral("pathSpacing"), 3}}}});
    const auto typography = controller.photoLayerTypography(0);
    const auto preview = render_output_image_with_backend(source,
        EditorPreviewControllerTestAccess::state(controller), QColorSpace(QColorSpace::SRgb),
        QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    const auto preview_path = QDir(receipt_dir).filePath(QStringLiteral("contour-preflight.png"));
    const auto preview_saved = !preview.isNull() && preview.save(preview_path);
    std::cerr << "A6 contour preflight: " << QJsonDocument::fromVariant(QVariantMap{
        {QStringLiteral("addTextSucceeded"), add_text.value(QStringLiteral("ok"))},
        {QStringLiteral("attachSucceeded"), attach.value(QStringLiteral("ok"))},
        {QStringLiteral("styleSucceeded"), style.value(QStringLiteral("ok"))},
        {QStringLiteral("previewSaved"), preview_saved},
        {QStringLiteral("textLayerId"), text_id},
        {QStringLiteral("outlinePathCount"), a6_outline_paths.size()},
        {QStringLiteral("firstPathPointCount"), a6_outline_paths.isEmpty() ? 0 : a6_outline_paths.first().toList().size()},
        {QStringLiteral("storedBaselinePointCount"), typography.value(QStringLiteral("pathPoints")).toList().size()},
        {QStringLiteral("status"), controller.status()}})
        .toJson(QJsonDocument::Compact).toStdString() << '\n';
    return attach.value(QStringLiteral("ok")).toBool() ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  AgentEditController agent_edit;
  CatalogAssetModel catalog_model;
  PhotoAutomationBridge bridge(&agent_edit, &catalog_model);
  bridge.setCompositionHandler([&controller](const QVariantMap& command) {
    return controller.photoCompositionCommand(command);
  });
  if (!bridge.start(true, {QStringLiteral("read"), QStringLiteral("draft")})) {
    std::cerr << bridge.errorMessage().toStdString() << '\n';
    return fail("Start the authenticated production automation bridge on localhost");
  }
  AgentMcpClient client;
  client.setEndpoint(bridge.mcpEndpoint());
  client.setBearerToken(bridge.token());
  client.setTimeoutMs(60000);
  client.setAllowedTools({QStringLiteral("photo.inspect_composition"),
      QStringLiteral("photo.edit_composition")});
  const auto composition_tool_names = QStringList{
      QStringLiteral("photo.inspect_composition"), QStringLiteral("photo.edit_composition")};
  const auto discovery = client.refreshToolCache(QStringLiteral("production-bridge-real-photo"));
  if (!discovery.value(QStringLiteral("ok")).toBool() ||
      !client.hasCachedTool(QStringLiteral("photo.edit_composition")))
    return fail("Discover the production composition commands through authenticated MCP");
  if (!journal.record(QStringLiteral("tools_discovered"), QVariantMap{
          {QStringLiteral("endpointKind"), bridge.mcpEndpointKind()},
          {QStringLiteral("toolNames"), composition_tool_names}}))
    return fail("Update the attempt journal after MCP tool discovery");

  QVariantMap style;
  for (const auto& value : built_in_agent_edit_styles()) {
    const auto candidate = value.toMap();
    const auto style_id = candidate.value(QStringLiteral("id")).toString();
    if (style_id == (is_a1 ? QStringLiteral("warm-editorial-wedding")
                           : QStringLiteral("natural-portrait"))) {
      style = candidate;
      break;
    }
  }
  if (style.isEmpty()) return fail("Find the pinned style used for this agent run");
  const QString brief = is_a1 ? QStringLiteral(
      "Create a restrained editorial portrait composition with the exact headline FLOAT across the upper "
      "torso. Use a large warm light serif with a dark fine stroke and subtle shadow. Put the person in front "
      "of the headline by independently copying the selected subject pixels above it. Use assetId composition "
      "and the existing maskId sam2-subject-selection. Return exactly two ordered photo.edit_composition "
      "calls: add_text with text FLOAT at x=950,y=1500, then copy_selection with that maskId and independent=true. "
      "Do not invent ids or make global image adjustments. Keep the text editable.")
      : is_a2 ? QStringLiteral(
      "Design a photographic print cover titled WEATHER / MEMORY. Fit the title with a real supported variable-font "
      "width axis; use restrained two-plate drift and paper/ink character with an explicit seed; add one drypoint "
      "annotation that catches the facade grain. Keep the photograph recognizable, preserve editable type and "
      "effect controls, and do not add generic dirt.")
      : is_a4 ? QStringLiteral(
      "Make a late-show frame with the exact words AFTER THE LAST SONG. Keep the performer crisp with the supplied "
      "protection mask, add a small warm halation around stage lights, pull ambient light into one directional "
      "flash-drag trail, and paint a narrow tapered afterglow along the bright stage-light path. Keep the face "
      "free of halos and preserve editable effect, mask, and paint controls.")
      : is_a5 ? QStringLiteral(
      "Create a refined glass-lettered product photograph with the exact small headline HELD IN LIGHT in the "
      "open area beside the bottle. Use editable smoked-glass typography that samples only the image below its "
      "text layer. Protect the readable bottle label and bright highlights; keep the words readable at fit view and avoid "
      "a bright outline or broad blur. Preserve the photograph and all text controls.")
      : is_a6 ? QStringLiteral(
      "Create a spare monochrome field-note image titled NORTH / 04. Use the supplied closed selection as a "
      "detached text baseline, respecting its holes and negative space. Keep the type editable and the photograph's "
      "monochrome tone unchanged; show text overflow rather than silently clipping it. Use the existing maskId "
      "lyskamm-contour-selection and assetId composition.")
      : QStringLiteral(
      "Build a small photographic collage from the supplied glass photograph. Select the bright curved reflection "
      "along the upper-right rim as a reusable Photo Fragment. Capture it as an independent transparent cutout, "
      "save it as a named reusable preset, hide the captured tip layer, then place a short restrained sequence of "
      "seeded marks following the glass rim on a separate paint layer. Preserve antialiased alpha and any transparent "
      "holes in the captured detail; keep the original photograph recognizable and avoid random scatter. "
      "Every composition tool call must use the exact active assetId composition; never invent an asset ID.");
  AgentLocalModelPlanRequest request;
  request.model_id = model_id;
  request.style = style;
  request.asset = {{QStringLiteral("assetId"), QStringLiteral("composition")},
      {QStringLiteral("filePath"), source_path}};
  request.current_adjustment_patch_json = QStringLiteral(
      "{\"kind\":\"photo-editor-adjustment-patch\",\"version\":1,"
      "\"scope\":\"asset-bound\",\"groups\":{}}");
  const auto phase_one_instructions = is_a1 ? brief : is_a4 ? brief + QStringLiteral(
      "\nFor this first planning round, return exactly three ordered photo.edit_composition calls: add_source_layer for the "
      "original photograph, add_text with the exact text AFTER THE LAST SONG in the calm upper-left dark sky, then "
      "add_pixel_layer for the independent light-paint stroke. For every call, the assetId field must be exactly the string "
      "\"composition\" (literal value composition, without angle brackets, dimensions, or a placeholder). For add_text values use only text, x, and y; "
      "place the editable headline near x=55,y=34 so it sits above the stage lamps. Do not add effects or paint in this round. "
      "Use integer coordinates.") : is_a5 ? brief + QStringLiteral(
      "\nFor this first planning round, inspect the supplied bottle photograph and return exactly one "
      "photo.edit_composition call: add_text with text HELD IN LIGHT on the open area beside the bottle. "
      "For add_text, values may contain only text, x, and y; material and other typography style fields belong "
      "to the separate typography operation after the text layer ID is returned. Use assetId composition and "
      "integer canvas-pixel coordinates inside the image; do not add an outline, shadow, effect, or paint layer "
      "in this round. Keep the phrase exact and editable. The canvas dimensions are 768 by 1024 pixels; "
      "the exact assetId is composition (the dimensions are not an asset ID). Use assetId composition exactly. "
      "Place the headline in the empty right margin beside the label, approximately x=485,y=540, and do not cover the label.") : is_a6 ? brief + QStringLiteral(
      "\nFor this first planning round only, add the exact editable text NORTH / 04 at an approximate anchor x=900,y=900. "
      "Return exactly one photo.edit_composition call with operation add_text and values containing only text, x, and y. "
      "Use assetId composition exactly. The canvas is 2350 by 2000 pixels. Do not invent IDs or change the photograph.") : is_a3 ? brief + QStringLiteral(
      "\nFor this first planning round, inspect the supplied photograph and return exactly one "
      "photo.edit_composition call: lasso_selection with a closed polygon of at least 10 normalized points "
      "around only the narrow bright curved reflection on the upper-right glass rim. On this photo the rim "
      "arc is approximately x=0.64..0.91 and y=0.03..0.15 in normalized image coordinates. Trace only the "
      "20-40 source-pixel highlight band. Pair each point on the outer edge with a nearby return point on the "
      "inner edge, no more than 0.02 normalized coordinates apart. Useful boundary anchors are outer "
      "[(0.64,0.03),(0.70,0.04),(0.76,0.055),(0.82,0.075),(0.87,0.10),(0.91,0.13)] and inner "
      "[(0.90,0.145),(0.86,0.115),(0.81,0.09),(0.75,0.07),(0.69,0.055),(0.64,0.045)] in reverse order. "
      "Keep polygon area below 0.008 of the image; do not include the broad glass opening or background. "
      "Use enough points to follow the curve smoothly, not a five-point wedge. Include feather=0.005 in the "
      "lasso values to create a soft-edged cutout. Keep every point within [0,1]. "
      "Use assetId composition exactly. Wait for the returned createdMaskId before copying pixels.") : brief + QStringLiteral(
      "\nFor this first planning round only, create the linked editable photo layer needed to keep the print "
      "effect separate from the original base image. Return exactly one photo.edit_composition call: "
      "add_source_layer with assetId composition. Wait for its returned layer ID before planning dependent edits.");
  request.user_instructions = phase_one_instructions;
  if (!journal.record(QStringLiteral("prompt_ready"), QVariantMap{
          {QStringLiteral("creativeBrief"), brief},
          {QStringLiteral("phaseInstructions"), phase_one_instructions},
          {QStringLiteral("availableToolNames"), composition_tool_names}}))
    return fail("Update the attempt journal with the exact fixed brief before model planning");
  for (const auto& tool_value : discovery.value(QStringLiteral("tools")).toList()) {
    const auto tool = tool_value.toMap();
    const auto tool_name = tool.value(QStringLiteral("name")).toString();
    if (composition_tool_names.contains(tool_name) && ((!is_a3 && !is_a4 && !is_a5 && !is_a6) ||
        tool_name == QStringLiteral("photo.edit_composition"))) {
      auto model_tool = tool;
      if (is_a3 || is_a4 || is_a5 || is_a6) {
        model_tool.remove(QStringLiteral("description"));
        model_tool.remove(QStringLiteral("title"));
      }
      request.available_tool_schemas.append(model_tool);
    }
  }
  if (request.available_tool_schemas.size() != ((is_a3 || is_a4 || is_a5 || is_a6) ? 1 : composition_tool_names.size()))
    return fail("Restrict the model prompt to the registered composition tools needed for this brief");
  const auto make_a4_tool_schema = [](const QVariantList& operations, const QVariantMap& value_properties) {
    QVariantMap operation_property{{QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("enum"), operations}};
    const QVariantMap properties{
        {QStringLiteral("assetId"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")}}},
        {QStringLiteral("operation"), operation_property},
        {QStringLiteral("layerId"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")}}},
        {QStringLiteral("values"), QVariantMap{{QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), value_properties}}}};
    return QVariantMap{
        {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
        {QStringLiteral("description"), QStringLiteral("Create and edit the exact photo composition layers and effects.")},
        {QStringLiteral("requiredCapability"), QStringLiteral("draft")},
        {QStringLiteral("inputSchema"), QVariantMap{{QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), properties},
            {QStringLiteral("required"), QVariantList{QStringLiteral("assetId"),
                QStringLiteral("operation"), QStringLiteral("values")}}}}};
  };
  const auto schema_field = [](const QString& type) {
    return QVariantMap{{QStringLiteral("type"), type}};
  };
  const auto schema_enum_field = [](const QStringList& values) {
    QVariantList enum_values;
    for (const auto& value : values) enum_values.append(value);
    return QVariantMap{{QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("enum"), enum_values}};
  };
  if (is_a4) {
    request.available_tool_schemas = {make_a4_tool_schema(
        {QStringLiteral("add_source_layer"), QStringLiteral("add_text"), QStringLiteral("add_pixel_layer")},
        {{QStringLiteral("text"), schema_field(QStringLiteral("string"))},
         {QStringLiteral("x"), schema_field(QStringLiteral("integer"))},
         {QStringLiteral("y"), schema_field(QStringLiteral("integer"))}})};
  }
  request.allow_tool_call_plan = true;
  request.force_model_planning = true;
  request.allow_deterministic_fallback = false;
  request.strategy_phase = QStringLiteral("sample");
  QElapsedTimer planning_timer;
  if (!journal.record(QStringLiteral("model_planning"), QVariantMap{
          {QStringLiteral("qualifyingRun"), true}}))
    return fail("Record model invocation before beginning the qualifying attempt");
  planning_timer.start();
  const auto plan = plan_agent_recipe_patch(request);
  const auto planning_elapsed_ms = planning_timer.elapsed();
  if (!journal.record(QStringLiteral("model_plan_returned"), QVariantMap{
          {QStringLiteral("modelPlan"), QVariantMap{
              {QStringLiteral("ok"), plan.ok},
              {QStringLiteral("decision"), plan.decision},
              {QStringLiteral("error"), plan.error_message},
              {QStringLiteral("summary"), plan.plan_summary},
              {QStringLiteral("toolCalls"), plan.tool_call_plan},
              {QStringLiteral("promptAudit"), plan.prompt_audit},
              {QStringLiteral("elapsedMs"), planning_elapsed_ms}}}}))
    return fail("Persist the model response before validating its selected calls");
  if (!plan.ok || plan.decision != QStringLiteral("tool_call_plan") ||
      plan.tool_call_plan.size() != (is_a1 ? 2 : is_a4 ? 3 : 1) ||
      plan.prompt_audit.value(QStringLiteral("backend")).toString() != QStringLiteral("ollama") ||
      plan.prompt_audit.value(QStringLiteral("fallbackUsed")).toBool()) {
    std::cerr << "model plan: " << QJsonDocument::fromVariant(QVariantMap{
        {QStringLiteral("error"), plan.error_message},
        {QStringLiteral("audit"), plan.prompt_audit},
        {QStringLiteral("calls"), plan.tool_call_plan}}).toJson(QJsonDocument::Compact).toStdString()
        << '\n';
    return fail("The local model must return the required concrete composition calls without fallback");
  }
  if (is_a3) {
    const auto lasso_call = plan.tool_call_plan.size() == 1
        ? normalize_live_composition_tool_call(plan.tool_call_plan.front()) : QVariantMap{};
    const auto arguments = lasso_call.value(QStringLiteral("arguments")).toMap();
    const auto values = arguments.value(QStringLiteral("values")).toMap();
    const auto points = values.value(QStringLiteral("points")).toList();
    double twice_area = 0.0;
    for (qsizetype index = 0; index < points.size(); ++index) {
      const auto current = points.at(index).toMap();
      const auto next = points.at((index + 1) % points.size()).toMap();
      twice_area += current.value(QStringLiteral("x")).toDouble() * next.value(QStringLiteral("y")).toDouble() -
          next.value(QStringLiteral("x")).toDouble() * current.value(QStringLiteral("y")).toDouble();
    }
    const auto feather = values.value(QStringLiteral("feather")).toDouble();
    if (lasso_call.isEmpty() ||
        arguments.value(QStringLiteral("operation")).toString() != QStringLiteral("lasso_selection") ||
        points.size() < 10 || points.size() > 128 || std::abs(twice_area) * 0.5 > 0.008 ||
        feather < 0.003 || feather > 0.01)
      return fail("A3 lasso must be a narrow feathered reflection band; reject broad or unfeathered paths before dispatch");
  }
  if (is_a5) {
    const auto text_call = plan.tool_call_plan.size() == 1
        ? normalize_live_composition_tool_call(plan.tool_call_plan.front()) : QVariantMap{};
    const auto arguments = text_call.value(QStringLiteral("arguments")).toMap();
    const auto values = arguments.value(QStringLiteral("values")).toMap();
    const auto x = values.value(QStringLiteral("x")).toDouble();
    const auto y = values.value(QStringLiteral("y")).toDouble();
    if (text_call.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        arguments.value(QStringLiteral("assetId")).toString() != QStringLiteral("composition") ||
        arguments.value(QStringLiteral("operation")).toString() != QStringLiteral("add_text") ||
        values.value(QStringLiteral("text")).toString() != QStringLiteral("HELD IN LIGHT") ||
        values.contains(QStringLiteral("material")) || !std::isfinite(x) || !std::isfinite(y) ||
        x < 460 || x > 510 || y < 450 || y > 620)
      return fail("A5 first round must add only the exact headline in the bottle's open side area");
  }
  if (is_a4) {
    const auto first = normalize_live_composition_tool_call(plan.tool_call_plan.at(0));
    const auto second = normalize_live_composition_tool_call(plan.tool_call_plan.at(1));
    const auto third = normalize_live_composition_tool_call(plan.tool_call_plan.at(2));
    const auto first_args = first.value(QStringLiteral("arguments")).toMap();
    const auto second_args = second.value(QStringLiteral("arguments")).toMap();
    const auto second_values = second_args.value(QStringLiteral("values")).toMap();
    const auto third_args = third.value(QStringLiteral("arguments")).toMap();
    if (first.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        first_args.value(QStringLiteral("assetId")).toString() != QStringLiteral("composition") ||
        first_args.value(QStringLiteral("operation")).toString() != QStringLiteral("add_source_layer") ||
        second.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        second_args.value(QStringLiteral("assetId")).toString() != QStringLiteral("composition") ||
        second_args.value(QStringLiteral("operation")).toString() != QStringLiteral("add_text") ||
        second_values.value(QStringLiteral("text")).toString() != QStringLiteral("AFTER THE LAST SONG") ||
        second_values.value(QStringLiteral("x")).toDouble() < 40 || second_values.value(QStringLiteral("x")).toDouble() > 80 ||
        second_values.value(QStringLiteral("y")).toDouble() < 20 || second_values.value(QStringLiteral("y")).toDouble() > 50 ||
        third.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        third_args.value(QStringLiteral("assetId")).toString() != QStringLiteral("composition") ||
        third_args.value(QStringLiteral("operation")).toString() != QStringLiteral("add_pixel_layer"))
      return fail("A4 first phase must target assetId composition and create source, exact headline, and paint layers in order");
  }
  if (!journal.record(QStringLiteral("model_plan_ready"), QVariantMap{
          {QStringLiteral("modelPlan"), QVariantMap{
              {QStringLiteral("decision"), plan.decision},
              {QStringLiteral("summary"), plan.plan_summary},
              {QStringLiteral("toolCalls"), plan.tool_call_plan},
              {QStringLiteral("promptAudit"), plan.prompt_audit},
              {QStringLiteral("elapsedMs"), planning_elapsed_ms}}}}))
    return fail("Persist the model-selected operations before dispatch");

  QVariantList reports;
  auto saw_text = false;
  auto saw_copy = false;
  auto saw_paint_layer = false;
  auto saw_source_layer = false;
  QString text_layer_id;
  QString paint_layer_id;
  QString source_layer_id;
  QString a3_mask_id;
  QString a3_tip_layer_id;
  QVariantMap a2_layer_creation_plan;
  QImage text_only;
  for (const auto& call_value : plan.tool_call_plan) {
    const auto call = normalize_live_composition_tool_call(call_value);
    if (call.isEmpty()) return fail("Model tool call must identify the registered composition tool and typed arguments");
    const auto name = call.value(QStringLiteral("name")).toString();
    const auto arguments = call.value(QStringLiteral("arguments")).toMap();
    const auto operation = arguments.value(QStringLiteral("operation")).toString();
    const auto values = arguments.value(QStringLiteral("values")).toMap();
    const auto requested_text = values.value(QStringLiteral("text")).toString();
    saw_text = saw_text || (name == QStringLiteral("photo.edit_composition") &&
        operation == QStringLiteral("add_text") &&
        (is_a1 ? requested_text == QStringLiteral("FLOAT")
               : is_a4 ? requested_text == QStringLiteral("AFTER THE LAST SONG")
               : is_a5 ? requested_text == QStringLiteral("HELD IN LIGHT")
               : is_a6 ? requested_text == QStringLiteral("NORTH / 04")
                       : requested_text == QStringLiteral("WEATHER / MEMORY")));
    saw_copy = saw_copy || (name == QStringLiteral("photo.edit_composition") &&
        operation == QStringLiteral("copy_selection") &&
        values.value(QStringLiteral("maskId")).toString() == QStringLiteral("sam2-subject-selection") &&
        values.value(QStringLiteral("independent")).toBool());
    saw_paint_layer = saw_paint_layer || (name == QStringLiteral("photo.edit_composition") &&
        operation == QStringLiteral("add_pixel_layer"));
    saw_source_layer = saw_source_layer || (name == QStringLiteral("photo.edit_composition") &&
        operation == QStringLiteral("add_source_layer"));
    const auto result = client.callTool(name, arguments);
    if (!result.value(QStringLiteral("ok")).toBool()) {
      std::cerr << "MCP call: " << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).toStdString()
                << '\n';
      return fail("Execute each model-selected operation successfully through PhotoAutomationBridge");
    }
    reports.append(QVariantMap{{QStringLiteral("name"), name},
        {QStringLiteral("arguments"), arguments}, {QStringLiteral("modelOutput"), call.value(QStringLiteral("modelOutput"))},
        {QStringLiteral("result"), result}});
    if (is_a2 || is_a3 || is_a4 || is_a5) {
      auto model_result = result;
      if (is_a3) {
        const auto full_content = result.value(QStringLiteral("structuredContent")).toMap();
        QVariantMap compact_content{{QStringLiteral("ok"), result.value(QStringLiteral("ok"))},
            {QStringLiteral("changed"), full_content.value(QStringLiteral("changed"))},
            {QStringLiteral("paintBusy"), full_content.value(QStringLiteral("paintBusy"))}};
        if (operation == QStringLiteral("lasso_selection"))
          compact_content.insert(QStringLiteral("createdMaskId"),
              full_content.value(QStringLiteral("createdMaskId")));
        if (operation == QStringLiteral("add_pixel_layer") || operation == QStringLiteral("add_source_layer"))
          compact_content.insert(QStringLiteral("createdLayerId"),
              full_content.value(QStringLiteral("createdLayerId")));
        model_result = {{QStringLiteral("ok"), result.value(QStringLiteral("ok"))},
            {QStringLiteral("structuredContent"), compact_content}};
      }
      request.prior_tool_results.append(QVariantMap{
          {QStringLiteral("name"), name}, {QStringLiteral("arguments"), arguments},
          {QStringLiteral("result"), model_result}});
    }
    if (operation == QStringLiteral("add_pixel_layer"))
      paint_layer_id = result.value(QStringLiteral("structuredContent")).toMap()
          .value(QStringLiteral("createdLayerId")).toString();
    if (operation == QStringLiteral("add_source_layer"))
      source_layer_id = result.value(QStringLiteral("structuredContent")).toMap()
          .value(QStringLiteral("createdLayerId")).toString();
    if (operation == QStringLiteral("lasso_selection"))
      a3_mask_id = result.value(QStringLiteral("structuredContent")).toMap()
          .value(QStringLiteral("createdMaskId")).toString();
    if (!journal.record(QStringLiteral("initial_operations_dispatched"), QVariantMap{
            {QStringLiteral("toolReports"), reports}}))
      return fail("Persist each completed model-selected MCP operation");
    if (operation == QStringLiteral("add_text")) {
      const auto layers = controller.photoLayers();
      if (layers.isEmpty()) return fail("The production controller must return the new text layer");
      text_layer_id = layers.front().toMap().value(QStringLiteral("id")).toString();
      text_only = render_output_image_with_backend(source,
          EditorPreviewControllerTestAccess::state(controller), QColorSpace(QColorSpace::SRgb),
          QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    }
  }
  if (is_a3) {
    const auto lasso_call = plan.tool_call_plan.size() == 1
        ? normalize_live_composition_tool_call(plan.tool_call_plan.front()) : QVariantMap{};
    const auto lasso_arguments = lasso_call.value(QStringLiteral("arguments")).toMap();
    const auto lasso_values = lasso_arguments.value(QStringLiteral("values")).toMap();
    const auto lasso_points = lasso_values.value(QStringLiteral("points")).toList();
    const auto lasso_feather = lasso_values.value(QStringLiteral("feather")).toDouble();
    if (lasso_call.isEmpty() ||
        lasso_call.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        lasso_arguments.value(QStringLiteral("operation")).toString() != QStringLiteral("lasso_selection") ||
        lasso_points.size() < 10 || lasso_points.size() > 128 ||
        lasso_feather < 0.003 || lasso_feather > 0.01 || a3_mask_id.isEmpty())
      return fail("A3 must trace the curved rim with at least ten lasso points and use its returned mask ID");
    if (!journal.record(QStringLiteral("a3_lasso_validated"), QVariantMap{
            {QStringLiteral("maskId"), a3_mask_id}, {QStringLiteral("pointCount"), lasso_points.size()},
            {QStringLiteral("lassoCall"), lasso_call}}))
      return fail("Persist the accepted A3 reflection selection");

    QVariantList a3_model_plans;
    const auto make_a3_plan = [&](const QString& phase, const QString& instructions,
                                  int expected_calls) {
      // The first round already saw the photograph and chose the selection.
      // Dependent rounds need only the returned IDs, not repeated image tokens.
      request.asset = {{QStringLiteral("assetId"), QStringLiteral("composition")}};
      request.user_instructions = instructions;
      QElapsedTimer timer;
      timer.start();
      const auto next_plan = plan_agent_recipe_patch(request);
      const auto elapsed_ms = timer.elapsed();
      const QVariantMap receipt{{QStringLiteral("phase"), phase},
          {QStringLiteral("decision"), next_plan.decision},
          {QStringLiteral("ok"), next_plan.ok}, {QStringLiteral("error"), next_plan.error_message},
          {QStringLiteral("expectedToolCallCount"), expected_calls},
          {QStringLiteral("toolCalls"), next_plan.tool_call_plan},
          {QStringLiteral("promptAudit"), next_plan.prompt_audit},
          {QStringLiteral("elapsedMs"), elapsed_ms}};
      a3_model_plans.append(receipt);
      journal.record(QStringLiteral("a3_%1_plan_returned").arg(phase),
          QVariantMap{{QStringLiteral("plan"), receipt}});
      return next_plan;
    };
    const auto plan_is_valid = [&](const AgentLocalModelPlanResult& value, int expected_calls) {
      return value.ok && value.decision == QStringLiteral("tool_call_plan") &&
          value.tool_call_plan.size() == expected_calls &&
          value.prompt_audit.value(QStringLiteral("backend")).toString() == QStringLiteral("ollama") &&
          !value.prompt_audit.value(QStringLiteral("fallbackUsed")).toBool();
    };
    const auto append_and_dispatch = [&](const QVariant& model_value, const QString& phase) {
      const auto call = normalize_live_composition_tool_call(model_value);
      if (call.isEmpty()) return QVariantMap{};
      const auto name = call.value(QStringLiteral("name")).toString();
      const auto arguments = call.value(QStringLiteral("arguments")).toMap();
      const auto result = client.callTool(name, arguments);
      if (!result.value(QStringLiteral("ok")).toBool()) {
        std::cerr << "A3 " << phase.toStdString() << " MCP failure: "
                  << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
        return QVariantMap{};
      }
      const QVariantMap report{{QStringLiteral("name"), name},
          {QStringLiteral("arguments"), arguments},
          {QStringLiteral("modelOutput"), call.value(QStringLiteral("modelOutput"))},
          {QStringLiteral("result"), result}};
      reports.append(report);
      const auto operation = arguments.value(QStringLiteral("operation")).toString();
      const auto full_content = result.value(QStringLiteral("structuredContent")).toMap();
      QVariantMap compact_content{{QStringLiteral("ok"), result.value(QStringLiteral("ok"))},
          {QStringLiteral("changed"), full_content.value(QStringLiteral("changed"))},
          {QStringLiteral("paintBusy"), full_content.value(QStringLiteral("paintBusy"))}};
      if (operation == QStringLiteral("copy_selection") || operation == QStringLiteral("add_pixel_layer"))
        compact_content.insert(QStringLiteral("createdLayerId"), full_content.value(QStringLiteral("createdLayerId")));
      if (operation == QStringLiteral("save_photo_fragment_preset"))
        compact_content.insert(QStringLiteral("photoFragmentBrushPresets"),
            full_content.value(QStringLiteral("photoFragmentBrushPresets")));
      request.prior_tool_results.append(QVariantMap{{QStringLiteral("name"), name},
          {QStringLiteral("arguments"), arguments},
          {QStringLiteral("result"), QVariantMap{{QStringLiteral("ok"), result.value(QStringLiteral("ok"))},
              {QStringLiteral("structuredContent"), compact_content}}}});
      journal.record(QStringLiteral("a3_%1_dispatched").arg(phase),
          QVariantMap{{QStringLiteral("toolReports"), reports}});
      return report;
    };

    request.user_instructions = QStringLiteral(
        "Continue the A3 edit using the successful lasso result above. The returned selection mask ID is %1. Return exactly two "
        "ordered photo.edit_composition calls: copy_selection with maskId %1 and independent=true to make "
        "a self-contained pixel tip, then add_pixel_layer {} for a separate transparent paint layer. Do not "
        "invent IDs or alter the source photograph.").arg(a3_mask_id);
    const auto cutout_plan = make_a3_plan(QStringLiteral("cutout"), request.user_instructions, 2);
    if (!plan_is_valid(cutout_plan, 2))
      return fail("A3 must model-plan an independent selected cutout and its separate paint layer");
    auto cutout_call = normalize_live_composition_tool_call(cutout_plan.tool_call_plan.at(0));
    auto layer_call = normalize_live_composition_tool_call(cutout_plan.tool_call_plan.at(1));
    auto cutout_args = cutout_call.value(QStringLiteral("arguments")).toMap();
    auto layer_args = layer_call.value(QStringLiteral("arguments")).toMap();
    if (cutout_call.isEmpty() || layer_call.isEmpty() ||
        cutout_args.value(QStringLiteral("operation")).toString() != QStringLiteral("copy_selection") ||
        cutout_args.value(QStringLiteral("values")).toMap().value(QStringLiteral("maskId")).toString() != a3_mask_id ||
        !cutout_args.value(QStringLiteral("values")).toMap().value(QStringLiteral("independent")).toBool() ||
        layer_args.value(QStringLiteral("operation")).toString() != QStringLiteral("add_pixel_layer"))
      return fail("A3 cutout calls must use the selected mask and return an independent tip plus paint layer");
    const auto cutout_report = append_and_dispatch(cutout_plan.tool_call_plan.at(0), QStringLiteral("cutout"));
    if (cutout_report.isEmpty()) return fail("Create the A3 independent glass-reflection cutout through MCP");
    a3_tip_layer_id = cutout_report.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("structuredContent")).toMap().value(QStringLiteral("createdLayerId")).toString();
    QElapsedTimer cutout_timer;
    cutout_timer.start();
    while (controller.photoPaintBusy() && cutout_timer.elapsed() < 60000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      QThread::msleep(5);
    }
    if (controller.photoPaintBusy() || !controller.photoPaintError().isEmpty())
      return fail("Wait for A3 selected pixels to materialize before creating dependent layers");
    const auto paint_layer_report = append_and_dispatch(cutout_plan.tool_call_plan.at(1), QStringLiteral("paint_layer"));
    if (paint_layer_report.isEmpty()) return fail("Create the A3 separate transparent paint layer through MCP");
    paint_layer_id = paint_layer_report.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("structuredContent")).toMap().value(QStringLiteral("createdLayerId")).toString();
    if (a3_tip_layer_id.isEmpty() || paint_layer_id.isEmpty())
      return fail("A3 must use the real IDs returned for the independent tip and paint layer");

    request.user_instructions = QStringLiteral(
        "Continue the A3 edit using the successful independent cutout result above. Its actual pixel-layer ID is %1. Save this "
        "layer as a reusable photo-fragment brush preset named A3 Glass Rim %2. This is a long narrow curved "
        "highlight tip, so preserve its alignment and scale along the glass arc: set rotationVariation=0 and "
        "scaleVariation=0. Return exactly one "
        "photo.edit_composition call for save_photo_fragment_preset with values {name: A3 Glass Rim %2, "
        "tipLayerId: %1, rotationVariation: 0, scaleVariation: 0}. Use the actual returned ID.")
        .arg(a3_tip_layer_id).arg(attempt);
    const auto preset_plan = make_a3_plan(QStringLiteral("preset"), request.user_instructions, 1);
    if (!plan_is_valid(preset_plan, 1))
      return fail("A3 must model-plan saving its returned cutout as a reusable Photo Fragment preset");
    const auto preset_call = normalize_live_composition_tool_call(preset_plan.tool_call_plan.front());
    const auto preset_args = preset_call.value(QStringLiteral("arguments")).toMap();
    const auto preset_values = preset_args.value(QStringLiteral("values")).toMap();
    const auto expected_preset_name = QStringLiteral("A3 Glass Rim %1").arg(attempt);
    if (preset_call.isEmpty() || preset_args.value(QStringLiteral("operation")).toString() !=
        QStringLiteral("save_photo_fragment_preset") ||
        preset_values.value(QStringLiteral("name")).toString() != expected_preset_name ||
        preset_values.value(QStringLiteral("tipLayerId")).toString() != a3_tip_layer_id ||
        preset_values.value(QStringLiteral("rotationVariation")).toDouble() != 0.0 ||
        preset_values.value(QStringLiteral("scaleVariation")).toDouble() != 0.0)
      return fail("A3 preset must reference the exact selected pixel layer and fixed run name");
    const auto preset_report = append_and_dispatch(preset_plan.tool_call_plan.front(), QStringLiteral("preset"));
    if (preset_report.isEmpty()) return fail("Save the A3 Photo Fragment preset through production MCP");

    request.user_instructions = QStringLiteral(
        "Continue the A3 edit. The saved self-contained preset is named %1 and the separate transparent paint layer ID is %2. "
        "Hide the captured tip layer %3 by setting its opacity to 0, then paint on layer %2 using the saved "
        "preset. Return exactly these two ordered photo.edit_composition call shapes, replacing only the "
        "paint values with your chosen path and seed: first arguments {assetId: composition, operation: "
        "opacity, layerId: %3, values: {opacity: 0}}; second arguments {assetId: composition, operation: "
        "paint, layerId: %2, values: {textureMaterial: photo_fragment, tipPresetName: %1, textureSeed: "
        "integer from 1 to 4294967295, size: 55 to 90, spacing: 0.10 to 0.14, opacity: 0.35 to 0.50, "
        "tipRotationVariation: 0, tipScaleVariation: 0, points: at least four ABSOLUTE "
        "source-canvas PIXEL coordinates following the upper-left glass rim}}. This image is %4 by %5 pixels. "
        "Do not use normalized 0-to-1 fractions for paint points; paint uses pixels. Echo the captured "
        "reflection along the curved upper-left rim, away from the source highlight. Follow these "
        "curve anchors in order, adding intermediate points as needed: %6; %7; %8; %9; %10. "
        "The path must move left-to-right while moving upward toward the top-center; do not draw a diagonal "
        "across the glass opening. Do not stamp over the selected source area. Use canvas pixels well above 1. "
        "The layerId field belongs at the call's top level, not inside values. Use the exact IDs and preset "
        "name above; do not invent IDs or call tools to inspect.")
        .arg(expected_preset_name, paint_layer_id, a3_tip_layer_id);
    request.user_instructions = request.user_instructions
        .arg(controller.sourceImageWidth()).arg(controller.sourceImageHeight())
        .arg(QStringLiteral("(%1,%2)").arg(static_cast<int>(controller.sourceImageWidth() * 0.04))
            .arg(static_cast<int>(controller.sourceImageHeight() * 0.25)))
        .arg(QStringLiteral("(%1,%2)").arg(static_cast<int>(controller.sourceImageWidth() * 0.12))
            .arg(static_cast<int>(controller.sourceImageHeight() * 0.15)))
        .arg(QStringLiteral("(%1,%2)").arg(static_cast<int>(controller.sourceImageWidth() * 0.23))
            .arg(static_cast<int>(controller.sourceImageHeight() * 0.07)))
        .arg(QStringLiteral("(%1,%2)").arg(static_cast<int>(controller.sourceImageWidth() * 0.35))
            .arg(static_cast<int>(controller.sourceImageHeight() * 0.03)))
        .arg(QStringLiteral("(%1,%2)").arg(static_cast<int>(controller.sourceImageWidth() * 0.46))
            .arg(static_cast<int>(controller.sourceImageHeight() * 0.02)));
    const auto paint_plan = make_a3_plan(QStringLiteral("paint"), request.user_instructions, 2);
    if (!plan_is_valid(paint_plan, 2))
      return fail("A3 must model-plan hiding the tip and painting a seeded sequence from its saved preset");
    const auto hide_call = normalize_live_composition_tool_call(paint_plan.tool_call_plan.at(0));
    const auto stamp_call = normalize_live_composition_tool_call(paint_plan.tool_call_plan.at(1));
    const auto hide_args = hide_call.value(QStringLiteral("arguments")).toMap();
    const auto stamp_args = stamp_call.value(QStringLiteral("arguments")).toMap();
    const auto stamp_values = stamp_args.value(QStringLiteral("values")).toMap();
    const auto stamp_points = stamp_values.value(QStringLiteral("points")).toList();
    const auto seed_type = stamp_values.value(QStringLiteral("textureSeed")).metaType().id();
    const auto integer_seed = seed_type == QMetaType::Int || seed_type == QMetaType::UInt ||
        seed_type == QMetaType::LongLong || seed_type == QMetaType::ULongLong;
    if (hide_call.isEmpty() || stamp_call.isEmpty() ||
        hide_args.value(QStringLiteral("operation")).toString() != QStringLiteral("opacity") ||
        hide_args.value(QStringLiteral("layerId")).toString() != a3_tip_layer_id ||
        hide_args.value(QStringLiteral("values")).toMap().value(QStringLiteral("opacity")).toDouble() != 0.0 ||
        stamp_args.value(QStringLiteral("operation")).toString() != QStringLiteral("paint") ||
        stamp_args.value(QStringLiteral("layerId")).toString() != paint_layer_id ||
        stamp_values.value(QStringLiteral("textureMaterial")).toString() != QStringLiteral("photo_fragment") ||
        stamp_values.value(QStringLiteral("tipPresetName")).toString() != expected_preset_name ||
        stamp_values.value(QStringLiteral("tipRotationVariation")).toDouble() != 0.0 ||
        stamp_values.value(QStringLiteral("tipScaleVariation")).toDouble() != 0.0 ||
        stamp_values.value(QStringLiteral("spacing")).toDouble() < 0.10 ||
        stamp_values.value(QStringLiteral("spacing")).toDouble() > 0.14 ||
        stamp_points.size() < 4 || !integer_seed ||
        stamp_values.value(QStringLiteral("textureSeed")).toULongLong() == 0 ||
        stamp_values.value(QStringLiteral("textureSeed")).toULongLong() > 4294967295ULL)
      return fail("A3 stamp must use its saved preset after hiding the source tip and include a bounded seed and path");
    double previous_stamp_x = -1.0;
    double previous_stamp_y = std::numeric_limits<double>::infinity();
    double first_stamp_x = std::numeric_limits<double>::infinity();
    double last_stamp_x = -std::numeric_limits<double>::infinity();
    constexpr std::array<double, 5> rim_x_anchors{0.04, 0.12, 0.23, 0.35, 0.46};
    constexpr std::array<double, 5> rim_y_anchors{0.25, 0.15, 0.07, 0.03, 0.02};
    for (const auto& point_value : stamp_points) {
      const auto point = point_value.toMap();
      const auto x = point.value(QStringLiteral("x")).toDouble();
      const auto y = point.value(QStringLiteral("y")).toDouble();
      if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 ||
          x >= controller.sourceImageWidth() || y >= controller.sourceImageHeight())
        return fail("A3 fragment marks must stay inside the source canvas");
      const auto normalized_x = x / controller.sourceImageWidth();
      const auto normalized_y = y / controller.sourceImageHeight();
      if (normalized_x < rim_x_anchors.front() - 0.005 ||
          normalized_x > rim_x_anchors.back() + 0.005 ||
          normalized_x + 0.005 < previous_stamp_x || normalized_y > previous_stamp_y + 0.005)
        return fail("A3 paint path must trace the upper-left glass rim in left-to-right order");
      auto expected_y = rim_y_anchors.back();
      for (std::size_t i = 1; i < rim_x_anchors.size(); ++i) {
        if (normalized_x <= rim_x_anchors[i]) {
          const auto fraction = (normalized_x - rim_x_anchors[i - 1]) /
              (rim_x_anchors[i] - rim_x_anchors[i - 1]);
          expected_y = rim_y_anchors[i - 1] + fraction *
              (rim_y_anchors[i] - rim_y_anchors[i - 1]);
          break;
        }
      }
      if (std::abs(normalized_y - expected_y) > 0.035)
        return fail("A3 paint path points must remain close to the glass-rim curve anchors");
      if (previous_stamp_x < 0.0) first_stamp_x = normalized_x;
      previous_stamp_x = normalized_x;
      previous_stamp_y = normalized_y;
      last_stamp_x = normalized_x;
    }
    if (last_stamp_x - first_stamp_x < 0.25)
      return fail("A3 paint path must make a meaningful curved echo along the upper-left rim");
    if (append_and_dispatch(paint_plan.tool_call_plan.at(0), QStringLiteral("tip_hidden")).isEmpty() ||
        append_and_dispatch(paint_plan.tool_call_plan.at(1), QStringLiteral("paint")).isEmpty())
      return fail("Hide the captured tip and paint with the persisted Photo Fragment preset");
    QElapsedTimer paint_timer;
    paint_timer.start();
    while (controller.photoPaintBusy() && paint_timer.elapsed() < 60000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      QThread::msleep(5);
    }
    if (controller.photoPaintBusy() || !controller.photoPaintError().isEmpty())
      return fail("Wait for A3 fragment-stamp rasterization and require a clean paint result");

    const auto final_inspection = client.callTool(QStringLiteral("photo.inspect_composition"),
        {{QStringLiteral("assetId"), QStringLiteral("composition")}});
    if (!final_inspection.value(QStringLiteral("ok")).toBool())
      return fail("Inspect A3 layers and saved Photo Fragment preset through production MCP");
    const auto inspection_content = final_inspection.value(QStringLiteral("structuredContent")).toMap();
    auto preset_found = false;
    for (const auto& value : inspection_content.value(QStringLiteral("photoFragmentBrushPresets")).toList()) {
      preset_found = preset_found || value.toString() == expected_preset_name ||
          value.toMap().value(QStringLiteral("name")).toString() == expected_preset_name;
    }
    auto tip_hidden = false;
    auto paint_tiles_found = false;
    for (const auto& value : inspection_content.value(QStringLiteral("layers")).toList()) {
      const auto layer = value.toMap();
      const auto id = layer.value(QStringLiteral("id")).toString();
      if (id == a3_tip_layer_id) tip_hidden = !layer.value(QStringLiteral("visible"), true).toBool() ||
          layer.value(QStringLiteral("opacity"), 1.0).toDouble() == 0.0;
      if (id == paint_layer_id) paint_tiles_found = layer.value(QStringLiteral("kind")).toString() == QStringLiteral("pixel");
    }
    auto state = EditorPreviewControllerTestAccess::state(controller);
    for (const auto& layer_value : state.layers) {
      const auto layer = layer_value.toMap();
      if (layer.value(QStringLiteral("id")).toString() == paint_layer_id)
        paint_tiles_found = paint_tiles_found && !layer.value(QStringLiteral("paintTiles")).toMap().isEmpty();
    }
    if (!preset_found || !tip_hidden || !paint_tiles_found)
      return fail("A3 must retain the named preset, hidden source tip, and nonempty separate paint layer");
    const auto final_render = render_output_image_with_backend(source, state, QColorSpace(QColorSpace::SRgb),
        QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    const auto recipe = serialize_edit_recipe_json(state);
    const auto reopened = render_output_image_with_backend(source, parse_edit_recipe_json(recipe),
        QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    if (final_render.isNull() || reopened != final_render)
      return fail("A3 fragment composition must reopen pixel-identically from its editable recipe");
    const auto hash = [](const QString& path) {
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly)) return QString{};
      return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
    };
    const auto output_final = QDir(receipt_dir).filePath(QStringLiteral("attempt-final-render.png"));
    const auto output_base = QDir(receipt_dir).filePath(QStringLiteral("attempt-base-render.png"));
    const auto output_recipe = QDir(receipt_dir).filePath(QStringLiteral("final-recipe.json"));
    auto base_state = state;
    base_state.layers.clear();
    const auto base_render = render_output_image_with_backend(source, base_state, QColorSpace(QColorSpace::SRgb),
        QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    std::uint64_t changed_pixels = 0;
    for (int y = 0; y < source.height(); ++y) for (int x = 0; x < source.width(); ++x)
      if (final_render.pixel(x, y) != base_render.pixel(x, y)) ++changed_pixels;
    if (changed_pixels < 32 || base_render.isNull() || !base_render.save(output_base) || !final_render.save(output_final))
      return fail("A3 seeded photo-fragment marks must produce retained, measurable render changes");
    QFile recipe_file(output_recipe);
    if (!recipe_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        recipe_file.write(recipe.toUtf8()) != recipe.toUtf8().size())
      return fail("Save the editable A3 photographic collage recipe");
    recipe_file.close();
    const auto source_evidence = currentCreativeRunSourceEvidence();
    if (source_evidence.value(QStringLiteral("sourceRevision")).toString().isEmpty() ||
        source_evidence.value(QStringLiteral("sourceEvidenceSha256")).toString().isEmpty() ||
        !journal.record(QStringLiteral("source_evidence_captured"), source_evidence))
      return fail("Capture source revision and scoped dirty-worktree evidence for A3");
    QVariantMap receipt{{QStringLiteral("contract"), QStringLiteral("production-bridge-real-photo-creative-run-v1")},
        {QStringLiteral("briefId"), brief_id}, {QStringLiteral("attempt"), attempt},
        {QStringLiteral("modelId"), model_id}, {QStringLiteral("modelFamily"), probe.value(QStringLiteral("modelFamily"))},
        {QStringLiteral("ollamaModel"), probe.value(QStringLiteral("ollamaModel"))},
        {QStringLiteral("creativeBrief"), brief}, {QStringLiteral("phaseInstructions"), phase_one_instructions},
        {QStringLiteral("localOnly"), true}, {QStringLiteral("cloudUsed"), false},
        {QStringLiteral("automaticCommitAllowed"), false}, {QStringLiteral("productionBridge"), true},
        {QStringLiteral("endpointKind"), bridge.mcpEndpointKind()}, {QStringLiteral("realPhotoInput"), true},
        {QStringLiteral("countsTowardPlan18Runs"), true}, {QStringLiteral("sourceAssetId"), QStringLiteral("product-glass-bottle")},
        {QStringLiteral("sourcePath"), source_path}, {QStringLiteral("sourceSha256"), hash(source_path)},
        {QStringLiteral("modelPlan"), QVariantMap{{QStringLiteral("decision"), plan.decision},
            {QStringLiteral("toolCalls"), plan.tool_call_plan}, {QStringLiteral("promptAudit"), plan.prompt_audit},
            {QStringLiteral("elapsedMs"), planning_elapsed_ms}}},
        {QStringLiteral("a3DependentPlans"), a3_model_plans}, {QStringLiteral("toolReports"), reports},
        {QStringLiteral("layersTopFirst"), final_inspection.value(QStringLiteral("structuredContent")).toMap()
            .value(QStringLiteral("layers"))}, {QStringLiteral("selectionMaskId"), a3_mask_id},
        {QStringLiteral("tipLayerId"), a3_tip_layer_id}, {QStringLiteral("tipLayerHiddenAfterCapture"), tip_hidden},
        {QStringLiteral("paintLayerId"), paint_layer_id}, {QStringLiteral("fragmentPresetName"), expected_preset_name},
        {QStringLiteral("fragmentSeed"), stamp_values.value(QStringLiteral("textureSeed"))},
        {QStringLiteral("stampPointCount"), stamp_points.size()},
        {QStringLiteral("changedPixels"), QVariant::fromValue<qulonglong>(changed_pixels)},
        {QStringLiteral("finalRenderSha256"), hash(output_final)}, {QStringLiteral("recipeSha256"), hash(output_recipe)},
        {QStringLiteral("recipeReopenPixelIdentical"), true}, {QStringLiteral("paintError"), controller.photoPaintError()}};
    for (auto it = source_evidence.cbegin(); it != source_evidence.cend(); ++it) receipt.insert(it.key(), it.value());
    QFile receipt_file(receipt_path);
    if (!receipt_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        receipt_file.write(QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Indented)) <= 0)
      return fail("Persist the A3 MCP trace, render, recipe, and qualification evidence");
    if (!journal.finish(receipt_path)) return fail("Finalize the A3 append-safe run-status journal");
    bridge.stop();
    std::cout << "production_bridge_A3_photo_fragments=passed changed_pixels=" << changed_pixels
              << " receipt=" << receipt_file.fileName().toStdString() << '\n';
    return EXIT_SUCCESS;
  }
  if (is_a2) {
    const auto source_creation_call = plan.tool_call_plan.size() == 1
        ? normalize_live_composition_tool_call(plan.tool_call_plan.front()) : QVariantMap{};
    if (source_creation_call.isEmpty() ||
        source_creation_call.value(QStringLiteral("arguments")).toMap()
            .value(QStringLiteral("operation")).toString() != QStringLiteral("add_source_layer") ||
        source_layer_id.isEmpty())
      return fail("A2 first round must create the source-photo layer and use its returned ID");
    request.user_instructions = brief + QStringLiteral(
        "\nUse the successful add_source_layer result above. Now add editable title text with exact content "
        "WEATHER / MEMORY and create a separate transparent paint layer for a drypoint annotation. Return "
        "exactly two ordered photo.edit_composition calls: add_text, then add_pixel_layer. Use assetId "
        "composition, create title text only once, and wait for the returned IDs before styling or painting. "
        "The original photo canvas is 3648 by 2736 pixels. For the add_text x and y values, use integer "
        "pixel coordinates within those bounds, not normalized fractions; place the title in the lower "
        "part of the image (roughly x=1800, y=2320).");
    QElapsedTimer layer_plan_timer;
    layer_plan_timer.start();
    const auto layer_plan = plan_agent_recipe_patch(request);
    const auto layer_plan_elapsed_ms = layer_plan_timer.elapsed();
    a2_layer_creation_plan = QVariantMap{{QStringLiteral("decision"), layer_plan.decision},
        {QStringLiteral("summary"), layer_plan.plan_summary},
        {QStringLiteral("toolCalls"), layer_plan.tool_call_plan},
        {QStringLiteral("promptAudit"), layer_plan.prompt_audit},
        {QStringLiteral("elapsedMs"), layer_plan_elapsed_ms}};
    if (!journal.record(QStringLiteral("print_cover_layers_plan_returned"), QVariantMap{
            {QStringLiteral("printCoverLayerPlan"), a2_layer_creation_plan}}))
      return fail("Persist the dependent title and paint-layer plan before validating it");
    if (!journal.record(QStringLiteral("print_cover_layers_plan_validating"), QVariantMap{
            {QStringLiteral("printCoverLayerPlan"), a2_layer_creation_plan},
            {QStringLiteral("layerPlanOk"), layer_plan.ok},
            {QStringLiteral("decision"), layer_plan.decision},
            {QStringLiteral("toolCallCount"), layer_plan.tool_call_plan.size()},
            {QStringLiteral("backend"), layer_plan.prompt_audit.value(QStringLiteral("backend"))},
            {QStringLiteral("fallbackUsed"), layer_plan.prompt_audit.value(QStringLiteral("fallbackUsed"))}}))
      return fail("Persist diagnostics for the dependent title and paint-layer plan");
    if (!layer_plan.ok || layer_plan.decision != QStringLiteral("tool_call_plan") ||
        layer_plan.tool_call_plan.size() != 2 ||
        layer_plan.prompt_audit.value(QStringLiteral("backend")).toString() != QStringLiteral("ollama") ||
        layer_plan.prompt_audit.value(QStringLiteral("fallbackUsed")).toBool()) {
      std::cerr << "A2 layer plan: " << QJsonDocument::fromVariant(QVariantMap{
          {QStringLiteral("error"), layer_plan.error_message},
          {QStringLiteral("audit"), layer_plan.prompt_audit},
          {QStringLiteral("calls"), layer_plan.tool_call_plan}})
          .toJson(QJsonDocument::Compact).toStdString() << '\n';
      return fail("The local model must plan editable title and paint layers from the returned source ID");
    }
    const auto title_call = normalize_live_composition_tool_call(layer_plan.tool_call_plan.at(0));
    const auto paint_layer_call = normalize_live_composition_tool_call(layer_plan.tool_call_plan.at(1));
    if (!journal.record(QStringLiteral("print_cover_layers_normalized"), QVariantMap{
            {QStringLiteral("titleCall"), title_call},
            {QStringLiteral("paintLayerCall"), paint_layer_call}}))
      return fail("Persist normalized A2 layer tool calls");
    if (title_call.isEmpty() || paint_layer_call.isEmpty())
      return fail("The dependent A2 plan must use the registered composition tool");
    const auto title_arguments = title_call.value(QStringLiteral("arguments")).toMap();
    const auto title_values = title_arguments.value(QStringLiteral("values")).toMap();
    const auto paint_layer_arguments = paint_layer_call.value(QStringLiteral("arguments")).toMap();
    if (title_call.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        title_arguments.value(QStringLiteral("operation")).toString() != QStringLiteral("add_text") ||
        title_values.value(QStringLiteral("text")).toString() != QStringLiteral("WEATHER / MEMORY") ||
        paint_layer_call.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        paint_layer_arguments.value(QStringLiteral("operation")).toString() != QStringLiteral("add_pixel_layer"))
      return fail("The A2 layer plan must add the fixed editable title, then its independent paint layer");
    for (const auto& model_call : layer_plan.tool_call_plan) {
      const auto call = normalize_live_composition_tool_call(model_call);
      if (call.isEmpty()) return fail("A2 layer plans must use the registered composition tool");
      const auto arguments = call.value(QStringLiteral("arguments")).toMap();
      const auto result = client.callTool(call.value(QStringLiteral("name")).toString(), arguments);
      if (!result.value(QStringLiteral("ok")).toBool()) {
        std::cerr << "A2 layer MCP dispatch failed: "
                  << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).toStdString()
                  << '\n';
        return fail("Execute model-selected title and paint-layer creation through production MCP");
      }
      reports.append(QVariantMap{{QStringLiteral("name"), call.value(QStringLiteral("name"))},
          {QStringLiteral("arguments"), arguments}, {QStringLiteral("modelOutput"), call.value(QStringLiteral("modelOutput"))},
          {QStringLiteral("result"), result}});
      request.prior_tool_results.append(QVariantMap{{QStringLiteral("name"), call.value(QStringLiteral("name"))},
          {QStringLiteral("arguments"), arguments}, {QStringLiteral("result"), result}});
      const auto operation = arguments.value(QStringLiteral("operation")).toString();
      const auto content = result.value(QStringLiteral("structuredContent")).toMap();
      if (operation == QStringLiteral("add_text")) {
        for (const auto& layer_value : content.value(QStringLiteral("layers")).toList()) {
          const auto layer = layer_value.toMap();
          if (layer.value(QStringLiteral("text")).toString() == QStringLiteral("WEATHER / MEMORY"))
            text_layer_id = layer.value(QStringLiteral("id")).toString();
        }
      } else if (operation == QStringLiteral("add_pixel_layer")) {
        paint_layer_id = content.value(QStringLiteral("createdLayerId")).toString();
      }
      if (!journal.record(QStringLiteral("print_cover_layers_dispatched"), QVariantMap{
              {QStringLiteral("toolReports"), reports}}))
        return fail("Persist the completed title and paint-layer operations");
    }
    saw_text = !text_layer_id.isEmpty();
    saw_paint_layer = !paint_layer_id.isEmpty();
    if (text_layer_id.isEmpty() || paint_layer_id.isEmpty())
      return fail("The A2 text and paint layers must return actual stable IDs");
  }
  if (is_a4) {
    if (plan.tool_call_plan.size() != 3 || text_layer_id.isEmpty() || paint_layer_id.isEmpty() ||
        source_layer_id.isEmpty() || !saw_source_layer || !saw_paint_layer || reports.size() != 3)
      return fail("A4 must create source, headline, and independent light-paint layers before effects are planned");
    const auto first = normalize_live_composition_tool_call(plan.tool_call_plan.at(0));
    const auto second = normalize_live_composition_tool_call(plan.tool_call_plan.at(1));
    const auto third = normalize_live_composition_tool_call(plan.tool_call_plan.at(2));
    const auto first_args = first.value(QStringLiteral("arguments")).toMap();
    const auto second_args = second.value(QStringLiteral("arguments")).toMap();
    const auto second_values = second_args.value(QStringLiteral("values")).toMap();
    const auto third_args = third.value(QStringLiteral("arguments")).toMap();
    if (first.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        first_args.value(QStringLiteral("assetId")).toString() != QStringLiteral("composition") ||
        first_args.value(QStringLiteral("operation")).toString() != QStringLiteral("add_source_layer") ||
        second.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        second_args.value(QStringLiteral("assetId")).toString() != QStringLiteral("composition") ||
        second_args.value(QStringLiteral("operation")).toString() != QStringLiteral("add_text") ||
        second_values.value(QStringLiteral("text")).toString() != QStringLiteral("AFTER THE LAST SONG") ||
        third.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        third_args.value(QStringLiteral("assetId")).toString() != QStringLiteral("composition") ||
        third_args.value(QStringLiteral("operation")).toString() != QStringLiteral("add_pixel_layer"))
      return fail("A4 first phase must add an editable source, exact headline, and separate paint layer in order");
  }
  if (!saw_text || text_layer_id.isEmpty() ||
      (is_a1 && !saw_copy) || (is_a2 && (!saw_paint_layer || paint_layer_id.isEmpty() ||
          !saw_source_layer || source_layer_id.isEmpty())) ||
      (is_a4 && (!saw_paint_layer || paint_layer_id.isEmpty())))
    return fail("The chosen operations must create the fixed editable text and required independent target layers");

  QString a2_source_layer_id;
  QVariantMap a2_width_axis;
  if (is_a1) {
  request.prior_tool_results = {QVariantMap{
      {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
      {QStringLiteral("arguments"), QVariantMap{
          {QStringLiteral("assetId"), QStringLiteral("composition")},
          {QStringLiteral("operation"), QStringLiteral("add_text")},
          {QStringLiteral("values"), QVariantMap{
              {QStringLiteral("text"), QStringLiteral("FLOAT")},
              {QStringLiteral("x"), 950}, {QStringLiteral("y"), 1500}}}}},
      {QStringLiteral("result"), QVariantMap{
          {QStringLiteral("ok"), true},
          {QStringLiteral("structuredContent"), QVariantMap{
              {QStringLiteral("layers"), QVariantList{QVariantMap{
                  {QStringLiteral("id"), text_layer_id},
                  {QStringLiteral("kind"), QStringLiteral("text")},
                  {QStringLiteral("text"), QStringLiteral("FLOAT")},
                  {QStringLiteral("position"), 0}}}}}}}}}};
  request.user_instructions = QStringLiteral(
      "Continue the same portrait edit using the exact tool results above. Style the editable FLOAT text "
      "layer whose returned id is %1. Return exactly one photo.edit_composition call: operation typography, "
      "layerId %1, values {family: Georgia, size: 140, weight: 700, color: #fff1d6, strokeWidth: 4, "
      "strokeColor: #ff17212a, shadowX: 3, shadowY: 5, shadowColor: #99000000}. Do not repeat add_text "
      "or copy_selection.").arg(text_layer_id);
  } else if (is_a5) {
    request.prior_tool_results = {QVariantMap{
        {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
        {QStringLiteral("arguments"), QVariantMap{
            {QStringLiteral("assetId"), QStringLiteral("composition")},
            {QStringLiteral("operation"), QStringLiteral("add_text")},
            {QStringLiteral("values"), QVariantMap{
                {QStringLiteral("text"), QStringLiteral("HELD IN LIGHT")}}}}},
        {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("ok"), true},
            {QStringLiteral("structuredContent"), QVariantMap{
                {QStringLiteral("layers"), QVariantList{QVariantMap{
                    {QStringLiteral("id"), text_layer_id},
                    {QStringLiteral("kind"), QStringLiteral("text")},
                    {QStringLiteral("text"), QStringLiteral("HELD IN LIGHT")}}}}}}}}}};
    request.asset = {{QStringLiteral("assetId"), QStringLiteral("composition")}};
    request.user_instructions = brief + QStringLiteral(
        "\nContinue the same bottle composition using the exact successful add_text result above. Apply one "
        "editable typography operation to the returned HELD IN LIGHT text layer ID %1. Use a restrained "
        "family Georgia and size 34 to 37 pixels so the 13-character line fits inside the 768-pixel canvas. "
        "Use smoked glass for clear fit-view contrast while preserving the product material. Set the exact schema fields "
        "material=smoked_plastic, materialTint=#ff263442 (eight-digit ARGB), materialTransmission between "
        "0.15 and 0.28, materialEdgeSoftness between 0.6 and 1.4, materialBlur=0, materialDepth=4, materialLightAngle=-45, "
        "strokeWidth=0, shadowX=0, and shadowY=0. "
        "Return exactly one photo.edit_composition call with "
        "operation typography and that exact layerId. Do not repeat add_text or invent IDs.").arg(text_layer_id);
  } else if (is_a6) {
    request.prior_tool_results = {QVariantMap{
        {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
        {QStringLiteral("arguments"), QVariantMap{
            {QStringLiteral("assetId"), QStringLiteral("composition")},
            {QStringLiteral("operation"), QStringLiteral("add_text")},
            {QStringLiteral("values"), QVariantMap{
                {QStringLiteral("text"), QStringLiteral("NORTH / 04")}}}}},
        {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("ok"), true},
            {QStringLiteral("structuredContent"), QVariantMap{
                {QStringLiteral("layers"), QVariantList{QVariantMap{
                    {QStringLiteral("id"), text_layer_id},
                    {QStringLiteral("kind"), QStringLiteral("text")},
                    {QStringLiteral("text"), QStringLiteral("NORTH / 04")}}}}}}}}}};
    request.asset = {{QStringLiteral("assetId"), QStringLiteral("composition")}};
    request.user_instructions = brief + QStringLiteral(
        "\nContinue the same monochrome field-note composition using the exact add_text result above. Return exactly two "
        "ordered photo.edit_composition calls, both targeting the returned text layer ID %1. First call operation text_path "
        "with values {maskId: lyskamm-contour-selection, contourIndex: 0}; this detaches the supplied selection contour as the "
        "editable baseline and retains its holes. Second call operation typography with values {family: Georgia, size: 58, "
        "weight: 500, color: #fff, strokeWidth: 1, strokeColor: #ff202020, pathSide: left, pathStart: 0.08, pathEnd: 0.92, "
        "pathSpacing: 3}. Do not add a fill, effect, or paint layer. Keep the exact NORTH / 04 text editable, show overflow "
        "rather than clipping, and do not alter the photo pixels or invent IDs.").arg(text_layer_id);
  } else if (is_a4) {
    const auto inspection = client.callTool(QStringLiteral("photo.inspect_composition"),
        {{QStringLiteral("assetId"), QStringLiteral("composition")}});
    if (!inspection.value(QStringLiteral("ok")).toBool())
      return fail("Inspect A4 source layers and selected mask through production MCP before planning dependent edits");
    const auto inspected_layers = inspection.value(QStringLiteral("structuredContent")).toMap()
        .value(QStringLiteral("layers")).toList();
    for (auto layer_it = inspected_layers.crbegin(); layer_it != inspected_layers.crend(); ++layer_it) {
      const auto layer = layer_it->toMap();
      if (layer.value(QStringLiteral("kind")).toString() == QStringLiteral("source")) {
        source_layer_id = layer.value(QStringLiteral("id")).toString();
        break;
      }
    }
    if (source_layer_id.isEmpty())
      return fail("Inspect the actual A4 source-photo layer ID before attaching editable light effects");
    request.asset = {{QStringLiteral("assetId"), QStringLiteral("composition")}};
    request.prior_tool_results.clear();
    request.prior_tool_results.append(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
        {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("assetId"), QStringLiteral("composition")},
            {QStringLiteral("operation"), QStringLiteral("add_source_layer")}, {QStringLiteral("values"), QVariantMap{}}}},
        {QStringLiteral("result"), QVariantMap{{QStringLiteral("ok"), true},
            {QStringLiteral("structuredContent"), QVariantMap{{QStringLiteral("createdLayerId"), source_layer_id}}}}}});
    request.prior_tool_results.append(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
        {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("assetId"), QStringLiteral("composition")},
            {QStringLiteral("operation"), QStringLiteral("add_text")},
            {QStringLiteral("values"), QVariantMap{{QStringLiteral("text"), QStringLiteral("AFTER THE LAST SONG")},
                {QStringLiteral("x"), 90}, {QStringLiteral("y"), 75}}}}},
        {QStringLiteral("result"), QVariantMap{{QStringLiteral("ok"), true},
            {QStringLiteral("structuredContent"), QVariantMap{{QStringLiteral("layers"), QVariantList{
                QVariantMap{{QStringLiteral("id"), text_layer_id}, {QStringLiteral("kind"), QStringLiteral("text")},
                    {QStringLiteral("text"), QStringLiteral("AFTER THE LAST SONG")}}}}}}}}});
    request.prior_tool_results.append(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
        {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("assetId"), QStringLiteral("composition")},
            {QStringLiteral("operation"), QStringLiteral("add_pixel_layer")}, {QStringLiteral("values"), QVariantMap{}}}},
        {QStringLiteral("result"), QVariantMap{{QStringLiteral("ok"), true},
            {QStringLiteral("structuredContent"), QVariantMap{{QStringLiteral("createdLayerId"), paint_layer_id}}}}}});
    QVariantList compact_layers;
    for (const auto& layer_value : inspected_layers) {
      const auto layer = layer_value.toMap();
      compact_layers.append(QVariantMap{{QStringLiteral("id"), layer.value(QStringLiteral("id"))},
          {QStringLiteral("kind"), layer.value(QStringLiteral("kind"))}});
    }
    request.prior_tool_results.append(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("photo.inspect_composition")},
        {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("assetId"), QStringLiteral("composition")}}},
        {QStringLiteral("result"), QVariantMap{{QStringLiteral("ok"), true},
            {QStringLiteral("structuredContent"), QVariantMap{{QStringLiteral("assetId"), QStringLiteral("composition")},
                {QStringLiteral("width"), controller.sourceImageWidth()},
                {QStringLiteral("height"), controller.sourceImageHeight()},
                {QStringLiteral("layers"), compact_layers}}}}}});
    request.user_instructions = brief + QStringLiteral(
        "\nContinue from the successful text and paint-layer results above. The exact headline layer is %1, the independent "
        "light-paint layer is %2, and the source-photo layer is %3. For each new call set assetId exactly to the literal "
        "string composition; returned layer IDs belong only in layerId, and the effect result ID belongs only in effectId. "
        "Return exactly three ordered photo.edit_composition calls: "
        "typography targeting headline layer %1 with family Georgia, size 44, weight 500, color #ffffe8c2, strokeWidth 1, "
        "strokeColor #aa18110b, and no shadow; add_effect targeting source layer %3 with effectType optical_diffusion, "
        "radius 4, highlightThreshold 0.86, highlightKnee 0.20, amount 0.14, warmth 0.2, and highlightProtection 0.4; "
        "then add_effect targeting source layer %3 with effectType dragged_shutter, distance 12, angle -144, falloff 0.5, "
        "highlightWeighting 0.6, amount 0.28, and protectSubject true. Keep the effects subtle and preserve the existing source pixels. "
        "Use the returned IDs above; do not create more layers, invent IDs, or add paint before the effect IDs are returned.")
        .arg(text_layer_id, paint_layer_id, source_layer_id);
    request.available_tool_schemas = {make_a4_tool_schema(
        {QStringLiteral("typography"), QStringLiteral("add_effect")},
        {{QStringLiteral("effectType"), schema_enum_field({QStringLiteral("optical_diffusion"),
             QStringLiteral("dragged_shutter")})},
         {QStringLiteral("family"), schema_field(QStringLiteral("string"))},
         {QStringLiteral("size"), schema_field(QStringLiteral("number"))},
         {QStringLiteral("weight"), schema_field(QStringLiteral("number"))},
         {QStringLiteral("color"), schema_field(QStringLiteral("string"))},
         {QStringLiteral("strokeWidth"), schema_field(QStringLiteral("number"))},
         {QStringLiteral("strokeColor"), schema_field(QStringLiteral("string"))},
         {QStringLiteral("radius"), schema_field(QStringLiteral("number"))},
         {QStringLiteral("highlightThreshold"), schema_field(QStringLiteral("number"))},
         {QStringLiteral("highlightKnee"), schema_field(QStringLiteral("number"))},
         {QStringLiteral("amount"), schema_field(QStringLiteral("number"))},
         {QStringLiteral("warmth"), schema_field(QStringLiteral("number"))},
         {QStringLiteral("highlightProtection"), schema_field(QStringLiteral("number"))},
         {QStringLiteral("distance"), schema_field(QStringLiteral("number"))},
         {QStringLiteral("angle"), schema_field(QStringLiteral("number"))},
         {QStringLiteral("falloff"), schema_field(QStringLiteral("number"))},
         {QStringLiteral("highlightWeighting"), schema_field(QStringLiteral("number"))},
         {QStringLiteral("protectSubject"), schema_field(QStringLiteral("boolean"))}})};
  } else {
    const auto axis_inspection = client.callTool(QStringLiteral("photo.inspect_composition"),
        {{QStringLiteral("assetId"), QStringLiteral("composition")},
         {QStringLiteral("values"), QVariantMap{{QStringLiteral("family"), QStringLiteral("Skia")}}}});
    if (!axis_inspection.value(QStringLiteral("ok")).toBool())
      return fail("Inspect installed variable font axes through production MCP before setting typography");
    const auto axis_content = axis_inspection.value(QStringLiteral("structuredContent")).toMap();
    for (const auto& value : axis_content.value(QStringLiteral("fontAxes")).toList()) {
      const auto axis = value.toMap();
      if (axis.value(QStringLiteral("tag")).toString().compare(QStringLiteral("wdth"), Qt::CaseInsensitive) == 0) {
        a2_width_axis = axis;
        break;
      }
    }
    if (a2_width_axis.isEmpty()) return fail("The selected installed font must expose its real WDTH variable axis");
    const auto inspected_layers = axis_content.value(QStringLiteral("layers")).toList();
    for (auto layer_it = inspected_layers.crbegin(); layer_it != inspected_layers.crend(); ++layer_it) {
      const auto layer = layer_it->toMap();
      if (layer.value(QStringLiteral("kind")).toString() == QStringLiteral("source")) {
        a2_source_layer_id = layer.value(QStringLiteral("id")).toString();
        break;
      }
    }
    if (a2_source_layer_id.isEmpty()) return fail("Inspect the actual base-photo layer ID before applying the print effect");
    QVariantList compact_layers;
    for (const auto& layer_value : inspected_layers) {
      const auto layer = layer_value.toMap();
      compact_layers.append(QVariantMap{{QStringLiteral("id"), layer.value(QStringLiteral("id"))},
          {QStringLiteral("kind"), layer.value(QStringLiteral("kind"))}});
    }
    request.prior_tool_results.clear();
    request.prior_tool_results.append(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
        {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("assetId"), QStringLiteral("composition")},
            {QStringLiteral("operation"), QStringLiteral("add_text")},
            {QStringLiteral("values"), QVariantMap{{QStringLiteral("text"), QStringLiteral("WEATHER / MEMORY")},
                {QStringLiteral("x"), 1800}, {QStringLiteral("y"), 2320}}}}},
        {QStringLiteral("result"), QVariantMap{{QStringLiteral("ok"), true},
            {QStringLiteral("structuredContent"), QVariantMap{{QStringLiteral("layers"), QVariantList{
                QVariantMap{{QStringLiteral("id"), text_layer_id}, {QStringLiteral("kind"), QStringLiteral("text")},
                    {QStringLiteral("text"), QStringLiteral("WEATHER / MEMORY")}}}}}}}}});
    request.prior_tool_results.append(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
        {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("assetId"), QStringLiteral("composition")},
            {QStringLiteral("operation"), QStringLiteral("add_pixel_layer")},
            {QStringLiteral("values"), QVariantMap{}}}},
        {QStringLiteral("result"), QVariantMap{{QStringLiteral("ok"), true},
            {QStringLiteral("structuredContent"), QVariantMap{{QStringLiteral("createdLayerId"), paint_layer_id}}}}}});
    request.prior_tool_results.append(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("photo.inspect_composition")},
        {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("assetId"), QStringLiteral("composition")},
            {QStringLiteral("values"), QVariantMap{{QStringLiteral("family"), QStringLiteral("Skia")}}}}},
        {QStringLiteral("result"), QVariantMap{{QStringLiteral("ok"), true},
            {QStringLiteral("structuredContent"), QVariantMap{{QStringLiteral("width"), controller.sourceImageWidth()},
                {QStringLiteral("height"), controller.sourceImageHeight()},
                {QStringLiteral("fontAxes"), QVariantList{a2_width_axis}},
                {QStringLiteral("layers"), compact_layers}}}}}});
    const auto minimum = a2_width_axis.value(QStringLiteral("minimum")).toDouble();
    const auto maximum = a2_width_axis.value(QStringLiteral("maximum")).toDouble();
    const auto default_value = a2_width_axis.value(QStringLiteral("default")).toDouble();
    request.user_instructions = brief + QStringLiteral(
        "\nContinue using the compact successful MCP results above. The editable title layer ID is %1; the independent paint "
        "layer ID is %2; the base photo layer ID is %3. The installed variable family is Skia and its actual "
        "supported width axis is %4 with range [%5,%6] and default %7. Return exactly three ordered "
        "photo.edit_composition calls, each with name photo.edit_composition and arguments containing "
        "assetId composition, operation, layerId, and values: typography targeting the title with family Skia and a restrained WDTH "
        "value inside that range and a readable title size between 150 and 220 pixels, add_effect targeting "
        "the base photo with effectType imperfect_print, an explicit integer seed, subtle opposite-signed "
        "redRegistrationX and blueRegistrationX values between 0.25 and 0.9 pixels in magnitude, paperTexture "
        "between 0.1 and 0.35, and inkSpread no greater than 0.12, then paint targeting the paint layer with one restrained dry_ink annotation stroke "
        "using at least three canvas-pixel points that follow a visible facade edge and an explicit textureSeed. Keep all three layers "
        "editable. Do not repeat add_text or add_pixel_layer, invent IDs, flatten, or change global adjustments.")
        .arg(text_layer_id, paint_layer_id, a2_source_layer_id,
            a2_width_axis.value(QStringLiteral("tag")).toString())
        .arg(minimum).arg(maximum).arg(default_value);
  }
  QElapsedTimer typography_timer;
  typography_timer.start();
  const auto typography_plan = plan_agent_recipe_patch(request);
  const auto typography_elapsed_ms = typography_timer.elapsed();
  if (!journal.record(QStringLiteral("creative_follow_up_plan_returned"), QVariantMap{
          {QStringLiteral("creativeFollowUpPlan"), QVariantMap{
              {QStringLiteral("decision"), typography_plan.decision},
              {QStringLiteral("toolCalls"), typography_plan.tool_call_plan},
              {QStringLiteral("promptAudit"), typography_plan.prompt_audit},
              {QStringLiteral("elapsedMs"), typography_elapsed_ms}}}}))
    return fail("Persist the A2 typed creative follow-up plan before validating it");
  if (!typography_plan.ok || typography_plan.decision != QStringLiteral("tool_call_plan") ||
      typography_plan.tool_call_plan.size() != ((is_a1 || is_a5) ? 1 : is_a6 ? 2 : 3) ||
      typography_plan.prompt_audit.value(QStringLiteral("backend")).toString() != QStringLiteral("ollama") ||
      typography_plan.prompt_audit.value(QStringLiteral("fallbackUsed")).toBool()) {
    std::cerr << "creative follow-up plan: " << QJsonDocument::fromVariant(QVariantMap{
        {QStringLiteral("error"), typography_plan.error_message},
        {QStringLiteral("audit"), typography_plan.prompt_audit},
        {QStringLiteral("calls"), typography_plan.tool_call_plan}})
        .toJson(QJsonDocument::Compact).toStdString() << '\n';
    return fail("The model must use returned layer IDs to plan the required typed creative-tool follow-up");
  }
  if (!journal.record(QStringLiteral("typography_plan_ready"), QVariantMap{
          {QStringLiteral("creativeFollowUpPlan"), QVariantMap{
              {QStringLiteral("decision"), typography_plan.decision},
              {QStringLiteral("toolCalls"), typography_plan.tool_call_plan},
              {QStringLiteral("promptAudit"), typography_plan.prompt_audit},
          {QStringLiteral("elapsedMs"), typography_elapsed_ms}}}}))
    return fail("Persist the model typography follow-up plan before dispatch");
  if (is_a6) {
    const auto path_call = normalize_live_composition_tool_call(typography_plan.tool_call_plan.at(0));
    const auto path_arguments = path_call.value(QStringLiteral("arguments")).toMap();
    const auto path_values = path_arguments.value(QStringLiteral("values")).toMap();
    const auto typography_call = normalize_live_composition_tool_call(typography_plan.tool_call_plan.at(1));
    const auto typography_arguments = typography_call.value(QStringLiteral("arguments")).toMap();
    const auto typography_values = typography_arguments.value(QStringLiteral("values")).toMap();
    if (path_call.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        path_arguments.value(QStringLiteral("assetId")).toString() != QStringLiteral("composition") ||
        path_arguments.value(QStringLiteral("operation")).toString() != QStringLiteral("text_path") ||
        path_arguments.value(QStringLiteral("layerId")).toString() != text_layer_id ||
        path_values.value(QStringLiteral("maskId")).toString() != QStringLiteral("lyskamm-contour-selection") ||
        path_values.value(QStringLiteral("contourIndex")).toInt() != 0 ||
        typography_call.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        typography_arguments.value(QStringLiteral("assetId")).toString() != QStringLiteral("composition") ||
        typography_arguments.value(QStringLiteral("operation")).toString() != QStringLiteral("typography") ||
        typography_arguments.value(QStringLiteral("layerId")).toString() != text_layer_id ||
        typography_values.value(QStringLiteral("family")).toString() != QStringLiteral("Georgia") ||
        typography_values.value(QStringLiteral("size")).toDouble() != 58.0 ||
        typography_values.value(QStringLiteral("color")).toString() != QStringLiteral("#fff") ||
        typography_values.value(QStringLiteral("pathSide")).toString() != QStringLiteral("left") ||
        typography_values.value(QStringLiteral("pathStart")).toDouble() < 0.08 ||
        typography_values.value(QStringLiteral("pathStart")).toDouble() > 0.12 ||
        typography_values.value(QStringLiteral("pathEnd")).toDouble() < 0.88 ||
        typography_values.value(QStringLiteral("pathEnd")).toDouble() > 0.92 ||
        typography_values.value(QStringLiteral("pathSpacing")).toDouble() < 2.0 ||
        typography_values.value(QStringLiteral("pathSpacing")).toDouble() > 4.0)
      return fail("A6 must attach the exact returned text layer to the supplied contour and style it with bounded editable typography");
    for (const auto& call : {path_call, typography_call}) {
      const auto arguments = call.value(QStringLiteral("arguments")).toMap();
      const auto result = client.callTool(call.value(QStringLiteral("name")).toString(), arguments);
      if (!result.value(QStringLiteral("ok")).toBool()) {
        std::cerr << "A6 MCP failure: "
                  << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
        return fail("Apply A6 contour typography through the production MCP bridge");
      }
      reports.append(QVariantMap{{QStringLiteral("name"), call.value(QStringLiteral("name"))},
          {QStringLiteral("arguments"), arguments},
          {QStringLiteral("modelOutput"), call.value(QStringLiteral("modelOutput"))},
          {QStringLiteral("result"), result}});
      if (!journal.record(QStringLiteral("contour_typography_dispatched"), QVariantMap{
              {QStringLiteral("toolReports"), reports}}))
        return fail("Persist each completed A6 contour typography operation");
    }
    const auto inspection = client.callTool(QStringLiteral("photo.inspect_composition"),
        {{QStringLiteral("assetId"), QStringLiteral("composition")}});
    if (!inspection.value(QStringLiteral("ok")).toBool())
      return fail("Inspect the final A6 contour text through production MCP");
    const auto final_layers = inspection.value(QStringLiteral("structuredContent")).toMap()
        .value(QStringLiteral("layers")).toList();
    auto editable_contour_text = false;
    for (const auto& layer_value : final_layers) {
      const auto layer = layer_value.toMap();
      if (layer.value(QStringLiteral("id")).toString() != text_layer_id) continue;
      const auto style = layer.value(QStringLiteral("textTypography")).toMap();
      const auto baseline_point_count = style.value(QStringLiteral("pathPoints")).toList().size();
      editable_contour_text = layer.value(QStringLiteral("kind")).toString() == QStringLiteral("text") &&
          layer.value(QStringLiteral("text")).toString() == QStringLiteral("NORTH / 04") &&
          baseline_point_count >= 3 && baseline_point_count <= 512 &&
          style.value(QStringLiteral("family")).toString() == QStringLiteral("Georgia");
    }
    if (!editable_contour_text)
      return fail("A6 must retain exact editable type and the detached contour geometry");
    const auto state = EditorPreviewControllerTestAccess::state(controller);
    const auto final_render = render_output_image_with_backend(source, state, QColorSpace(QColorSpace::SRgb),
        QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    auto base_state = state;
    base_state.layers.clear();
    const auto base_render = render_output_image_with_backend(source, base_state,
        QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    const auto recipe = serialize_edit_recipe_json(state);
    const auto reopened = render_output_image_with_backend(source, parse_edit_recipe_json(recipe),
        QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    if (final_render.isNull() || base_render.isNull() || reopened != final_render)
      return fail("Render A6 contour typography and prove exact saved-recipe reopen");
    std::uint64_t changed_pixels = 0;
    for (int y = 0; y < source.height(); ++y) for (int x = 0; x < source.width(); ++x)
      if (final_render.pixel(x, y) != base_render.pixel(x, y)) ++changed_pixels;
    if (changed_pixels < 100)
      return fail("A6 editable contour text must produce a measurable render while retaining the photo");
    const auto output_final = QDir(receipt_dir).filePath(QStringLiteral("attempt-final-render.png"));
    const auto output_base = QDir(receipt_dir).filePath(QStringLiteral("attempt-base-render.png"));
    const auto output_recipe = QDir(receipt_dir).filePath(QStringLiteral("final-recipe.json"));
    if (!final_render.save(output_final) || !base_render.save(output_base))
      return fail("Save A6 source and final render evidence");
    QFile recipe_file(output_recipe);
    if (!recipe_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        recipe_file.write(recipe.toUtf8()) != recipe.toUtf8().size())
      return fail("Save the editable A6 recipe");
    recipe_file.close();
    const auto hash = [](const QString& path) {
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly)) return QString{};
      return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
    };
    const auto source_evidence = currentCreativeRunSourceEvidence();
    if (source_evidence.value(QStringLiteral("sourceRevision")).toString().isEmpty() ||
        source_evidence.value(QStringLiteral("sourceEvidenceSha256")).toString().isEmpty() ||
        !journal.record(QStringLiteral("source_evidence_captured"), source_evidence))
      return fail("Capture source revision and scoped dirty-worktree evidence for A6");
    QVariantMap receipt{{QStringLiteral("contract"), QStringLiteral("production-bridge-real-photo-creative-run-v1")},
        {QStringLiteral("briefId"), brief_id}, {QStringLiteral("attempt"), attempt},
        {QStringLiteral("modelId"), model_id}, {QStringLiteral("modelFamily"), probe.value(QStringLiteral("modelFamily"))},
        {QStringLiteral("ollamaModel"), probe.value(QStringLiteral("ollamaModel"))},
        {QStringLiteral("creativeBrief"), brief}, {QStringLiteral("phaseInstructions"), phase_one_instructions},
        {QStringLiteral("localOnly"), true}, {QStringLiteral("cloudUsed"), false},
        {QStringLiteral("automaticCommitAllowed"), false}, {QStringLiteral("productionBridge"), true},
        {QStringLiteral("endpointKind"), bridge.mcpEndpointKind()}, {QStringLiteral("realPhotoInput"), true},
        {QStringLiteral("countsTowardPlan18Runs"), true}, {QStringLiteral("sourcePath"), source_path},
        {QStringLiteral("sourceSha256"), hash(source_path)}, {QStringLiteral("maskPath"), mask_path},
        {QStringLiteral("textLayerId"), text_layer_id}, {QStringLiteral("text"), QStringLiteral("NORTH / 04")},
        {QStringLiteral("contourMaskId"), QStringLiteral("lyskamm-contour-selection")},
        {QStringLiteral("changedPixels"), QVariant::fromValue<qulonglong>(changed_pixels)},
        {QStringLiteral("layersTopFirst"), final_layers}, {QStringLiteral("toolReports"), reports},
        {QStringLiteral("modelPlan"), QVariantMap{{QStringLiteral("decision"), plan.decision},
            {QStringLiteral("toolCalls"), plan.tool_call_plan}, {QStringLiteral("promptAudit"), plan.prompt_audit},
            {QStringLiteral("elapsedMs"), planning_elapsed_ms}}},
        {QStringLiteral("creativeFollowUpPlan"), QVariantMap{{QStringLiteral("decision"), typography_plan.decision},
            {QStringLiteral("toolCalls"), typography_plan.tool_call_plan},
            {QStringLiteral("promptAudit"), typography_plan.prompt_audit},
            {QStringLiteral("elapsedMs"), typography_elapsed_ms}}},
        {QStringLiteral("finalRenderSha256"), hash(output_final)}, {QStringLiteral("recipeSha256"), hash(output_recipe)},
        {QStringLiteral("recipeReopenPixelIdentical"), true},
        {QStringLiteral("finalRecipeSizeBytes"), QFileInfo(output_recipe).size()}};
    for (auto it = source_evidence.cbegin(); it != source_evidence.cend(); ++it) receipt.insert(it.key(), it.value());
    QFile receipt_file(receipt_path);
    if (!receipt_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        receipt_file.write(QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Indented)) <= 0)
      return fail("Persist the A6 MCP trace, contour render, recipe, and source evidence");
    if (!journal.finish(receipt_path)) return fail("Finalize the append-safe A6 run journal");
    bridge.stop();
    std::cout << "production_bridge_A6_contour_typography=passed changed_pixels=" << changed_pixels
              << " receipt=" << receipt_file.fileName().toStdString() << '\n';
    return EXIT_SUCCESS;
  }
  if (is_a4) {
    const auto follow_up_calls = typography_plan.tool_call_plan;
    const auto text_call = normalize_live_composition_tool_call(follow_up_calls.at(0));
    const auto glow_call = normalize_live_composition_tool_call(follow_up_calls.at(1));
    const auto trail_call = normalize_live_composition_tool_call(follow_up_calls.at(2));
    if (text_call.isEmpty() || glow_call.isEmpty() || trail_call.isEmpty())
      return fail("A4 follow-up edits must use registered typed composition operations");
    const auto text_args = text_call.value(QStringLiteral("arguments")).toMap();
    const auto text_values = text_args.value(QStringLiteral("values")).toMap();
    const auto glow_args = glow_call.value(QStringLiteral("arguments")).toMap();
    const auto glow_values = glow_args.value(QStringLiteral("values")).toMap();
    const auto trail_args = trail_call.value(QStringLiteral("arguments")).toMap();
    const auto trail_values = trail_args.value(QStringLiteral("values")).toMap();
    if (text_call.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        text_args.value(QStringLiteral("assetId")).toString() != QStringLiteral("composition") ||
        text_args.value(QStringLiteral("operation")).toString() != QStringLiteral("typography") ||
        text_args.value(QStringLiteral("layerId")).toString() != text_layer_id ||
        text_values.value(QStringLiteral("family")).toString() != QStringLiteral("Georgia") ||
        text_values.value(QStringLiteral("size")).toDouble() < 40 || text_values.value(QStringLiteral("size")).toDouble() > 48 ||
        glow_call.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        glow_args.value(QStringLiteral("assetId")).toString() != QStringLiteral("composition") ||
        glow_args.value(QStringLiteral("operation")).toString() != QStringLiteral("add_effect") ||
        glow_args.value(QStringLiteral("layerId")).toString() != source_layer_id ||
        glow_values.value(QStringLiteral("effectType")).toString() != QStringLiteral("optical_diffusion") ||
        glow_values.value(QStringLiteral("amount")).toDouble() < 0.08 || glow_values.value(QStringLiteral("amount")).toDouble() > 0.2 ||
        glow_values.value(QStringLiteral("warmth")).toDouble() < 0.1 || glow_values.value(QStringLiteral("warmth")).toDouble() > 0.4 ||
        trail_call.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        trail_args.value(QStringLiteral("assetId")).toString() != QStringLiteral("composition") ||
        trail_args.value(QStringLiteral("operation")).toString() != QStringLiteral("add_effect") ||
        trail_args.value(QStringLiteral("layerId")).toString() != source_layer_id ||
        trail_values.value(QStringLiteral("effectType")).toString() != QStringLiteral("dragged_shutter") ||
        trail_values.value(QStringLiteral("angle")).toDouble() < -180 ||
        trail_values.value(QStringLiteral("angle")).toDouble() > 180 ||
        trail_values.value(QStringLiteral("distance")).toDouble() < 8 || trail_values.value(QStringLiteral("distance")).toDouble() > 16 ||
        trail_values.value(QStringLiteral("amount")).toDouble() < 0.15 || trail_values.value(QStringLiteral("amount")).toDouble() > 0.4 ||
        !trail_values.value(QStringLiteral("protectSubject")).toBool())
      return fail("A4 must apply readable type, restrained warm highlight glow, and a subject-protected directional trail");
    QString optical_effect_id;
    QString shutter_effect_id;
    for (const auto& call : follow_up_calls) {
      const auto normalized = normalize_live_composition_tool_call(call);
      const auto arguments = normalized.value(QStringLiteral("arguments")).toMap();
      const auto result = client.callTool(normalized.value(QStringLiteral("name")).toString(), arguments);
      if (!result.value(QStringLiteral("ok")).toBool()) {
        std::cerr << "A4 MCP effect dispatch failed: "
                  << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
        return fail("Execute A4 typography and light effects through the authenticated production MCP bridge");
      }
      reports.append(QVariantMap{{QStringLiteral("name"), normalized.value(QStringLiteral("name"))},
          {QStringLiteral("arguments"), arguments}, {QStringLiteral("modelOutput"), normalized.value(QStringLiteral("modelOutput"))},
          {QStringLiteral("result"), result}});
      request.prior_tool_results.append(QVariantMap{{QStringLiteral("name"), normalized.value(QStringLiteral("name"))},
          {QStringLiteral("arguments"), arguments}, {QStringLiteral("result"), result}});
      const auto operation = arguments.value(QStringLiteral("operation")).toString();
      const auto returned_id = result.value(QStringLiteral("structuredContent")).toMap()
          .value(QStringLiteral("effectId")).toString();
      if (operation == QStringLiteral("add_effect") && arguments.value(QStringLiteral("values")).toMap()
              .value(QStringLiteral("effectType")).toString() == QStringLiteral("optical_diffusion"))
        optical_effect_id = returned_id;
      if (operation == QStringLiteral("add_effect") && arguments.value(QStringLiteral("values")).toMap()
              .value(QStringLiteral("effectType")).toString() == QStringLiteral("dragged_shutter"))
        shutter_effect_id = returned_id;
      if (!journal.record(QStringLiteral("a4_effects_and_type_dispatched"), QVariantMap{
              {QStringLiteral("toolReports"), reports}}))
        return fail("Persist each successful A4 title and effect edit");
    }
    if (optical_effect_id.isEmpty() || shutter_effect_id.isEmpty())
      return fail("A4 effects must return stable IDs before mask linking and painting are planned");
    request.prior_tool_results.clear();
    const auto record_prior = [&](const QString& operation, const QVariantMap& values,
                                  const QVariantMap& structured_result) {
      request.prior_tool_results.append(QVariantMap{
          {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("assetId"), QStringLiteral("composition")},
              {QStringLiteral("operation"), operation}, {QStringLiteral("values"), values}}},
          {QStringLiteral("result"), QVariantMap{{QStringLiteral("ok"), true},
              {QStringLiteral("structuredContent"), structured_result}}}});
    };
    record_prior(QStringLiteral("add_source_layer"), {},
        {{QStringLiteral("createdLayerId"), source_layer_id}});
    record_prior(QStringLiteral("add_text"),
        {{QStringLiteral("text"), QStringLiteral("AFTER THE LAST SONG")}, {QStringLiteral("x"), 55}, {QStringLiteral("y"), 34}},
        {{QStringLiteral("layers"), QVariantList{QVariantMap{
            {QStringLiteral("id"), text_layer_id}, {QStringLiteral("kind"), QStringLiteral("text")},
            {QStringLiteral("text"), QStringLiteral("AFTER THE LAST SONG")}}}}});
    record_prior(QStringLiteral("add_pixel_layer"), {}, {{QStringLiteral("createdLayerId"), paint_layer_id}});
    record_prior(QStringLiteral("typography"), {{QStringLiteral("family"), QStringLiteral("Georgia")},
        {QStringLiteral("size"), 44}}, {{QStringLiteral("layerId"), text_layer_id}});
    record_prior(QStringLiteral("add_effect"), {{QStringLiteral("effectType"), QStringLiteral("optical_diffusion")},
        {QStringLiteral("amount"), 0.14}, {QStringLiteral("warmth"), 0.2}},
        {{QStringLiteral("effectId"), optical_effect_id}});
    record_prior(QStringLiteral("add_effect"), {{QStringLiteral("effectType"), QStringLiteral("dragged_shutter")},
        {QStringLiteral("distance"), 12}, {QStringLiteral("amount"), 0.28},
        {QStringLiteral("protectSubject"), true}}, {{QStringLiteral("effectId"), shutter_effect_id}});
    request.asset = {{QStringLiteral("assetId"), QStringLiteral("composition")}};
    request.user_instructions = brief + QStringLiteral(
        "\nContinue using the successful MCP results above. For both calls set assetId to exactly the literal string "
        "composition. The generated source-layer ID and effect ID must never be used as assetId. The dragged-shutter effect ID is %1, the supplied performer "
        "protection mask ID is lit-performer-protection, and the separate paint layer ID is %2. Return exactly two ordered "
        "photo.edit_composition calls. For mask_effect the shape is {assetId: \"composition\", operation: \"mask_effect\", "
        "layerId: %3, values: {effectId: %1, maskId: lit-performer-protection}}. For paint the shape is "
        "{assetId: \"composition\", operation: \"paint\", layerId: %2, values: {textureMaterial: light_paint, "
        "color: #ffffd98a, size: 12, lightFalloff: 0.65, lightExposure: -0.3, lightTaper: 0.85, "
        "lightAccumulation: false, textureSeed: 4104, points: [{x: 610, y: 170, pressure: 0.45}, "
        "{x: 770, y: 245, pressure: 0.8}, {x: 925, y: 355, pressure: 0.55}]}}. Follow the existing diagonal stage-light "
        "path between the two bright lamps, stopping before the performer's face. Keep the stroke narrow, tapered, and subtle. "
        "Do not invent IDs or add any other operation.").arg(shutter_effect_id, paint_layer_id, source_layer_id);
    const QVariantMap light_point_schema{{QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), QVariantMap{{QStringLiteral("x"), schema_field(QStringLiteral("number"))},
            {QStringLiteral("y"), schema_field(QStringLiteral("number"))},
            {QStringLiteral("pressure"), schema_field(QStringLiteral("number"))}}},
        {QStringLiteral("required"), QVariantList{QStringLiteral("x"), QStringLiteral("y")}}};
    request.available_tool_schemas = {make_a4_tool_schema(
        {QStringLiteral("mask_effect"), QStringLiteral("paint")},
        {{QStringLiteral("effectId"), schema_field(QStringLiteral("string"))},
         {QStringLiteral("maskId"), schema_field(QStringLiteral("string"))},
         {QStringLiteral("textureMaterial"), schema_enum_field({QStringLiteral("light_paint")})},
         {QStringLiteral("color"), schema_field(QStringLiteral("string"))},
         {QStringLiteral("size"), schema_field(QStringLiteral("number"))},
         {QStringLiteral("lightFalloff"), schema_field(QStringLiteral("number"))},
         {QStringLiteral("lightExposure"), schema_field(QStringLiteral("number"))},
         {QStringLiteral("lightTaper"), schema_field(QStringLiteral("number"))},
         {QStringLiteral("lightAccumulation"), schema_field(QStringLiteral("boolean"))},
         {QStringLiteral("textureSeed"), schema_field(QStringLiteral("integer"))},
         {QStringLiteral("points"), QVariantMap{{QStringLiteral("type"), QStringLiteral("array")},
             {QStringLiteral("minItems"), 3}, {QStringLiteral("maxItems"), 8},
             {QStringLiteral("items"), light_point_schema}}}})};
    QElapsedTimer finishing_plan_timer;
    finishing_plan_timer.start();
    const auto finishing_plan = plan_agent_recipe_patch(request);
    const auto finishing_elapsed_ms = finishing_plan_timer.elapsed();
    if (!journal.record(QStringLiteral("a4_mask_and_paint_plan_returned"), QVariantMap{
            {QStringLiteral("ok"), finishing_plan.ok},
            {QStringLiteral("decision"), finishing_plan.decision},
            {QStringLiteral("summary"), finishing_plan.plan_summary},
            {QStringLiteral("error"), finishing_plan.error_message},
            {QStringLiteral("toolCalls"), finishing_plan.tool_call_plan},
            {QStringLiteral("promptAudit"), finishing_plan.prompt_audit},
            {QStringLiteral("elapsedMs"), finishing_elapsed_ms}}))
      return fail("Persist the model-selected A4 mask-link and tapered-paint plan");
    if (!finishing_plan.ok || finishing_plan.decision != QStringLiteral("tool_call_plan") ||
        finishing_plan.tool_call_plan.size() != 2 ||
        finishing_plan.prompt_audit.value(QStringLiteral("backend")).toString() != QStringLiteral("ollama") ||
        finishing_plan.prompt_audit.value(QStringLiteral("fallbackUsed")).toBool())
      return fail("The local vision model must plan the dependent A4 protection and light-paint calls without fallback");
    auto mask_call = normalize_live_composition_tool_call(finishing_plan.tool_call_plan.at(0));
    const auto paint_call = normalize_live_composition_tool_call(finishing_plan.tool_call_plan.at(1));
    auto mask_args = mask_call.value(QStringLiteral("arguments")).toMap();
    auto mask_values = mask_args.value(QStringLiteral("values")).toMap();
    if (mask_args.value(QStringLiteral("layerId")).toString().isEmpty() &&
        mask_values.value(QStringLiteral("layerId")).toString() == source_layer_id) {
      mask_values.remove(QStringLiteral("layerId"));
      mask_args.insert(QStringLiteral("layerId"), source_layer_id);
      mask_args.insert(QStringLiteral("values"), mask_values);
      mask_call.insert(QStringLiteral("arguments"), mask_args);
    }
    const auto paint_args = paint_call.value(QStringLiteral("arguments")).toMap();
    const auto paint_values = paint_args.value(QStringLiteral("values")).toMap();
    const auto points = paint_values.value(QStringLiteral("points")).toList();
    if (mask_call.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        mask_args.value(QStringLiteral("assetId")).toString() != QStringLiteral("composition") ||
        mask_args.value(QStringLiteral("operation")).toString() != QStringLiteral("mask_effect") ||
        mask_args.value(QStringLiteral("layerId")).toString() != source_layer_id ||
        mask_values.value(QStringLiteral("effectId")).toString() != shutter_effect_id ||
        mask_values.value(QStringLiteral("maskId")).toString() != QStringLiteral("lit-performer-protection") ||
        paint_call.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        paint_args.value(QStringLiteral("assetId")).toString() != QStringLiteral("composition") ||
        paint_args.value(QStringLiteral("operation")).toString() != QStringLiteral("paint") ||
        paint_args.value(QStringLiteral("layerId")).toString() != paint_layer_id ||
        paint_values.value(QStringLiteral("textureMaterial")).toString() != QStringLiteral("light_paint") ||
        paint_values.value(QStringLiteral("size")).toDouble() < 8 || paint_values.value(QStringLiteral("size")).toDouble() > 15 ||
        paint_values.value(QStringLiteral("lightExposure")).toDouble() < -1 || paint_values.value(QStringLiteral("lightExposure")).toDouble() > 0 ||
        paint_values.value(QStringLiteral("lightTaper")).toDouble() < 0.75 ||
        paint_values.value(QStringLiteral("lightFalloff")).toDouble() < 0.4 || points.size() < 3 || points.size() > 8)
      return fail("A4 final phase must link the returned shutter effect to the supplied mask and paint a tapered light trail");
    for (const auto& point_value : points) {
      const auto point = point_value.toMap();
      const auto x = point.value(QStringLiteral("x")).toDouble();
      const auto y = point.value(QStringLiteral("y")).toDouble();
      if (!std::isfinite(x) || !std::isfinite(y) || x < 550 || x > 1000 || y < 120 || y > 420)
        return fail("A4 afterglow points must follow the existing two-lamp diagonal without crossing the performer face");
    }
    for (const auto& call : {mask_call, paint_call}) {
      const auto arguments = call.value(QStringLiteral("arguments")).toMap();
      const auto result = client.callTool(call.value(QStringLiteral("name")).toString(), arguments);
      if (!result.value(QStringLiteral("ok")).toBool()) {
        std::cerr << "A4 final MCP dispatch failed: "
                  << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
        return fail("Execute A4 mask linking and light painting through production MCP");
      }
      reports.append(QVariantMap{{QStringLiteral("name"), call.value(QStringLiteral("name"))},
          {QStringLiteral("arguments"), arguments}, {QStringLiteral("modelOutput"), call.value(QStringLiteral("modelOutput"))},
          {QStringLiteral("result"), result}});
      request.prior_tool_results.append(QVariantMap{{QStringLiteral("name"), call.value(QStringLiteral("name"))},
          {QStringLiteral("arguments"), arguments}, {QStringLiteral("result"), result}});
      if (!journal.record(QStringLiteral("a4_protection_and_paint_dispatched"), QVariantMap{
              {QStringLiteral("toolReports"), reports}}))
        return fail("Persist each successful A4 protection and paint operation");
    }
    QElapsedTimer paint_timer;
    paint_timer.start();
    while (controller.photoPaintBusy() && paint_timer.elapsed() < 60000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      QThread::msleep(5);
    }
    if (controller.photoPaintBusy() || !controller.photoPaintError().isEmpty())
      return fail("Wait for the A4 tapered light-paint stroke to commit without a paint error");
    const auto final_inspection = client.callTool(QStringLiteral("photo.inspect_composition"),
        {{QStringLiteral("assetId"), QStringLiteral("composition")} });
    if (!final_inspection.value(QStringLiteral("ok")).toBool())
      return fail("Inspect the final A4 editable light composition through production MCP");
    const auto final_layers = final_inspection.value(QStringLiteral("structuredContent")).toMap()
        .value(QStringLiteral("layers")).toList();
    auto has_text = false;
    auto has_glow = false;
    auto has_shutter = false;
    auto has_masked_shutter = false;
    QString linked_shutter_mask_id;
    auto has_light_paint = false;
    for (const auto& layer_value : final_layers) {
      const auto layer = layer_value.toMap();
      if (layer.value(QStringLiteral("id")).toString() == text_layer_id)
        has_text = layer.value(QStringLiteral("kind")).toString() == QStringLiteral("text") &&
            layer.value(QStringLiteral("text")).toString() == QStringLiteral("AFTER THE LAST SONG");
      if (layer.value(QStringLiteral("id")).toString() == paint_layer_id)
        has_light_paint = layer.value(QStringLiteral("kind")).toString() == QStringLiteral("pixel");
      if (layer.value(QStringLiteral("id")).toString() != source_layer_id) continue;
      for (const auto& effect_value : layer.value(QStringLiteral("effects")).toList()) {
        const auto effect = effect_value.toMap();
        const auto type = effect.value(QStringLiteral("type")).toString();
        has_glow = has_glow || (type == QStringLiteral("optical_diffusion") &&
            effect.value(QStringLiteral("warmth")).toDouble() > 0);
        if (type == QStringLiteral("dragged_shutter")) {
          has_shutter = true;
          linked_shutter_mask_id = effect.value(QStringLiteral("maskId")).toString();
          has_masked_shutter = !linked_shutter_mask_id.isEmpty();
        }
      }
    }
    auto has_linked_effect_mask = false;
    for (const auto& mask_value : final_inspection.value(QStringLiteral("structuredContent")).toMap()
             .value(QStringLiteral("masks")).toList())
      has_linked_effect_mask = has_linked_effect_mask ||
          mask_value.toMap().value(QStringLiteral("id")).toString() == linked_shutter_mask_id;
    auto state = EditorPreviewControllerTestAccess::state(controller);
    auto has_committed_light_pixels = false;
    for (const auto& layer_value : state.layers) {
      const auto layer = layer_value.toMap();
      if (layer.value(QStringLiteral("id")).toString() != paint_layer_id) continue;
      has_committed_light_pixels = layer.value(QStringLiteral("kind")).toString() == QStringLiteral("pixel") &&
          !layer.value(QStringLiteral("paintTiles")).toMap().isEmpty();
      break;
    }
    if (!has_text || !has_glow || !has_shutter || !has_masked_shutter || !has_linked_effect_mask ||
        !has_light_paint || !has_committed_light_pixels)
      return fail("A4 final inspection must retain editable headline, warm glow, protected shutter, and painted trail");
    const auto final_render = render_output_image_with_backend(source, state, QColorSpace(QColorSpace::SRgb),
        QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    auto base_state = state;
    base_state.layers.clear();
    const auto base_render = render_output_image_with_backend(source, base_state, QColorSpace(QColorSpace::SRgb),
        QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    const auto recipe = serialize_edit_recipe_json(state);
    const auto reopened = render_output_image_with_backend(source, parse_edit_recipe_json(recipe),
        QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    if (final_render.isNull() || base_render.isNull() || reopened != final_render)
      return fail("Render A4 and prove that its saved editable recipe reopens pixel-identically");
    std::uint64_t changed_pixels = 0;
    for (int y = 0; y < source.height(); ++y) for (int x = 0; x < source.width(); ++x)
      if (final_render.pixel(x, y) != base_render.pixel(x, y)) ++changed_pixels;
    if (changed_pixels < 1000)
      return fail("A4 text, effects, and light-paint must create a measurable photo change");
    const auto output_final = QDir(receipt_dir).filePath(QStringLiteral("attempt-final-render.png"));
    const auto output_base = QDir(receipt_dir).filePath(QStringLiteral("attempt-base-render.png"));
    const auto output_recipe = QDir(receipt_dir).filePath(QStringLiteral("final-recipe.json"));
    if (!final_render.save(output_final) || !base_render.save(output_base))
      return fail("Save A4 source and final renders for visual inspection");
    QFile recipe_file(output_recipe);
    if (!recipe_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        recipe_file.write(recipe.toUtf8()) != recipe.toUtf8().size())
      return fail("Save the editable A4 recipe");
    recipe_file.close();
    const auto hash = [](const QString& path) {
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly)) return QString{};
      return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
    };
    const auto source_evidence = currentCreativeRunSourceEvidence();
    if (source_evidence.value(QStringLiteral("sourceRevision")).toString().isEmpty() ||
        source_evidence.value(QStringLiteral("sourceEvidenceSha256")).toString().isEmpty() ||
        !journal.record(QStringLiteral("source_evidence_captured"), source_evidence))
      return fail("Capture scoped source evidence for the A4 run receipt");
    QVariantMap receipt{{QStringLiteral("contract"), QStringLiteral("production-bridge-real-photo-creative-run-v1")},
        {QStringLiteral("briefId"), brief_id}, {QStringLiteral("attempt"), attempt},
        {QStringLiteral("modelId"), model_id}, {QStringLiteral("modelFamily"), probe.value(QStringLiteral("modelFamily"))},
        {QStringLiteral("ollamaModel"), probe.value(QStringLiteral("ollamaModel"))},
        {QStringLiteral("creativeBrief"), brief}, {QStringLiteral("localOnly"), true}, {QStringLiteral("cloudUsed"), false},
        {QStringLiteral("automaticCommitAllowed"), false}, {QStringLiteral("productionBridge"), true},
        {QStringLiteral("endpointKind"), bridge.mcpEndpointKind()}, {QStringLiteral("realPhotoInput"), true},
        {QStringLiteral("countsTowardPlan18Runs"), true}, {QStringLiteral("sourcePath"), source_path},
        {QStringLiteral("sourceSha256"), hash(source_path)}, {QStringLiteral("maskPath"), mask_path},
        {QStringLiteral("textLayerId"), text_layer_id}, {QStringLiteral("paintLayerId"), paint_layer_id},
        {QStringLiteral("sourceLayerId"), source_layer_id}, {QStringLiteral("opticalEffectId"), optical_effect_id},
        {QStringLiteral("draggedShutterEffectId"), shutter_effect_id},
        {QStringLiteral("paintPoints"), points}, {QStringLiteral("changedPixels"), QVariant::fromValue<qulonglong>(changed_pixels)},
        {QStringLiteral("layersTopFirst"), final_layers}, {QStringLiteral("toolReports"), reports},
        {QStringLiteral("modelPlan"), QVariantMap{{QStringLiteral("decision"), plan.decision},
            {QStringLiteral("toolCalls"), plan.tool_call_plan}, {QStringLiteral("promptAudit"), plan.prompt_audit},
            {QStringLiteral("elapsedMs"), planning_elapsed_ms}}},
        {QStringLiteral("creativeFollowUpPlan"), QVariantMap{{QStringLiteral("decision"), typography_plan.decision},
            {QStringLiteral("toolCalls"), typography_plan.tool_call_plan},
            {QStringLiteral("promptAudit"), typography_plan.prompt_audit},
            {QStringLiteral("elapsedMs"), typography_elapsed_ms}}},
        {QStringLiteral("finishingPlan"), QVariantMap{{QStringLiteral("decision"), finishing_plan.decision},
            {QStringLiteral("toolCalls"), finishing_plan.tool_call_plan},
            {QStringLiteral("promptAudit"), finishing_plan.prompt_audit},
            {QStringLiteral("elapsedMs"), finishing_elapsed_ms}}},
        {QStringLiteral("finalRenderSha256"), hash(output_final)}, {QStringLiteral("recipeSha256"), hash(output_recipe)},
        {QStringLiteral("recipeReopenPixelIdentical"), true}, {QStringLiteral("finalRecipeSizeBytes"), QFileInfo(output_recipe).size()}};
    for (auto it = source_evidence.cbegin(); it != source_evidence.cend(); ++it) receipt.insert(it.key(), it.value());
    QFile receipt_file(receipt_path);
    if (!receipt_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        receipt_file.write(QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Indented)) <= 0)
      return fail("Persist the A4 MCP trace, render, recipe, and source evidence");
    if (!journal.finish(receipt_path)) return fail("Finalize the A4 append-safe run-status journal");
    bridge.stop();
    std::cout << "production_bridge_A4_night_light_study=passed changed_pixels=" << changed_pixels
              << " receipt=" << receipt_file.fileName().toStdString() << '\n';
    return EXIT_SUCCESS;
  }
  if (is_a5) {
    const auto typography_call = normalize_live_composition_tool_call(typography_plan.tool_call_plan.front());
    const auto arguments = typography_call.value(QStringLiteral("arguments")).toMap();
    const auto values = arguments.value(QStringLiteral("values")).toMap();
    const auto material = values.value(QStringLiteral("material")).toString();
    const auto family = values.value(QStringLiteral("family")).toString();
    const auto size = values.value(QStringLiteral("size")).toDouble();
    const auto transmission = values.value(QStringLiteral("materialTransmission")).toDouble();
    const auto blur = values.value(QStringLiteral("materialBlur")).toDouble();
    const auto edge_softness = values.value(QStringLiteral("materialEdgeSoftness")).toDouble();
    if (typography_call.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        arguments.value(QStringLiteral("operation")).toString() != QStringLiteral("typography") ||
        arguments.value(QStringLiteral("layerId")).toString() != text_layer_id ||
        material != QStringLiteral("smoked_plastic") || family != QStringLiteral("Georgia") ||
        !std::isfinite(size) || size < 34 || size > 37 || !std::isfinite(transmission) ||
        transmission < 0.15 || transmission > 0.28 ||
        values.value(QStringLiteral("materialTint")).toString() != QStringLiteral("#ff263442") ||
        !std::isfinite(blur) || blur < 0 || blur > 1 ||
        !std::isfinite(edge_softness) || edge_softness < 0.6 || edge_softness > 1.4 ||
        values.value(QStringLiteral("strokeWidth")).toDouble() != 0.0 ||
        values.value(QStringLiteral("shadowX")).toDouble() != 0.0 ||
        values.value(QStringLiteral("shadowY")).toDouble() != 0.0)
      return fail("A5 must use readable smoked-glass typography on the exact returned editable text layer");
    const auto result = client.callTool(typography_call.value(QStringLiteral("name")).toString(), arguments);
    if (!result.value(QStringLiteral("ok")).toBool()) {
      std::cerr << "A5 typography MCP failure: "
                << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
      return fail("Apply the model-selected material typography through production MCP");
    }
    reports.append(QVariantMap{{QStringLiteral("name"), typography_call.value(QStringLiteral("name"))},
        {QStringLiteral("arguments"), arguments}, {QStringLiteral("modelOutput"), typography_call.value(QStringLiteral("modelOutput"))},
        {QStringLiteral("result"), result}});
    if (!journal.record(QStringLiteral("material_typography_dispatched"), QVariantMap{
            {QStringLiteral("toolReports"), reports}}))
      return fail("Persist the completed model-selected A5 material operation");
    const auto inspection = client.callTool(QStringLiteral("photo.inspect_composition"),
        {{QStringLiteral("assetId"), QStringLiteral("composition")}});
    if (!inspection.value(QStringLiteral("ok")).toBool())
      return fail("Inspect the final glass-lettered composition through production MCP");
    const auto final_layers = inspection.value(QStringLiteral("structuredContent")).toMap()
        .value(QStringLiteral("layers")).toList();
    auto editable_glass_text = false;
    auto text_fits_canvas = false;
    for (const auto& layer_value : final_layers) {
      const auto layer = layer_value.toMap();
      if (layer.value(QStringLiteral("id")).toString() != text_layer_id) continue;
      const auto style = layer.value(QStringLiteral("textTypography")).toMap();
      editable_glass_text = layer.value(QStringLiteral("kind")).toString() == QStringLiteral("text") &&
          layer.value(QStringLiteral("text")).toString() == QStringLiteral("HELD IN LIGHT") &&
          style.value(QStringLiteral("material")).toString() == QStringLiteral("smoked_plastic");
      const auto x = layer.value(QStringLiteral("x")).toDouble();
      const auto y = layer.value(QStringLiteral("y")).toDouble();
      const auto width = layer.value(QStringLiteral("pixelWidth")).toDouble();
      const auto height = layer.value(QStringLiteral("pixelHeight")).toDouble();
      text_fits_canvas = x >= 0 && y >= 0 && width > 0 && height > 0 &&
          x + width <= source.width() && y + height <= source.height();
    }
    if (!editable_glass_text || !text_fits_canvas)
      return fail("The inspected A5 layer must retain exact editable text and fit-view smoked-glass style");
    const auto state = EditorPreviewControllerTestAccess::state(controller);
    const auto final_render = render_output_image_with_backend(source, state, QColorSpace(QColorSpace::SRgb),
        QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    auto base_state = state;
    base_state.layers.clear();
    const auto base_render = render_output_image_with_backend(source, base_state,
        QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    const auto recipe = serialize_edit_recipe_json(state);
    const auto reopened = render_output_image_with_backend(source, parse_edit_recipe_json(recipe),
        QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    if (text_only.isNull() || final_render.isNull() || base_render.isNull() || reopened != final_render)
      return fail("Render the A5 glass lettering and prove exact serialized-recipe reopen");
    std::uint64_t material_changed_pixels = 0;
    std::uint64_t total_changed_pixels = 0;
    for (int y = 0; y < source.height(); ++y) for (int x = 0; x < source.width(); ++x) {
      if (final_render.pixel(x, y) != text_only.pixel(x, y)) ++material_changed_pixels;
      if (final_render.pixel(x, y) != base_render.pixel(x, y)) ++total_changed_pixels;
    }
    if (material_changed_pixels < 10 || total_changed_pixels < 1000)
      return fail("A5 material typography must measurably affect the photo while preserving the editable headline");
    const auto output_final = QDir(receipt_dir).filePath(QStringLiteral("attempt-final-render.png"));
    const auto output_text = QDir(receipt_dir).filePath(QStringLiteral("attempt-text-over-photo.png"));
    const auto output_base = QDir(receipt_dir).filePath(QStringLiteral("attempt-base-render.png"));
    const auto output_recipe = QDir(receipt_dir).filePath(QStringLiteral("final-recipe.json"));
    if (!text_only.save(output_text) || !base_render.save(output_base) || !final_render.save(output_final))
      return fail("Save A5 base, plain-text, and material render evidence");
    QFile recipe_file(output_recipe);
    if (!recipe_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        recipe_file.write(recipe.toUtf8()) != recipe.toUtf8().size())
      return fail("Save the editable A5 recipe");
    recipe_file.close();
    const auto hash = [](const QString& path) {
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly)) return QString{};
      return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
    };
    const auto source_evidence = currentCreativeRunSourceEvidence();
    if (source_evidence.value(QStringLiteral("sourceRevision")).toString().isEmpty() ||
        source_evidence.value(QStringLiteral("sourceEvidenceSha256")).toString().isEmpty() ||
        !journal.record(QStringLiteral("source_evidence_captured"), source_evidence))
      return fail("Capture source revision and scoped dirty-worktree evidence for A5");
    QVariantMap receipt{{QStringLiteral("contract"), QStringLiteral("production-bridge-real-photo-creative-run-v1")},
        {QStringLiteral("briefId"), brief_id}, {QStringLiteral("attempt"), attempt},
        {QStringLiteral("modelId"), model_id}, {QStringLiteral("modelFamily"), probe.value(QStringLiteral("modelFamily"))},
        {QStringLiteral("ollamaModel"), probe.value(QStringLiteral("ollamaModel"))},
        {QStringLiteral("creativeBrief"), brief}, {QStringLiteral("phaseInstructions"), phase_one_instructions},
        {QStringLiteral("localOnly"), true}, {QStringLiteral("cloudUsed"), false},
        {QStringLiteral("automaticCommitAllowed"), false}, {QStringLiteral("productionBridge"), true},
        {QStringLiteral("endpointKind"), bridge.mcpEndpointKind()}, {QStringLiteral("realPhotoInput"), true},
        {QStringLiteral("countsTowardPlan18Runs"), true},
        {QStringLiteral("sourcePath"), source_path}, {QStringLiteral("sourceSha256"), hash(source_path)},
        {QStringLiteral("textLayerId"), text_layer_id}, {QStringLiteral("material"), material},
        {QStringLiteral("materialTransmission"), transmission}, {QStringLiteral("materialBlur"), blur},
        {QStringLiteral("materialEdgeSoftness"), edge_softness},
        {QStringLiteral("materialChangedPixels"), QVariant::fromValue<qulonglong>(material_changed_pixels)},
        {QStringLiteral("totalChangedPixels"), QVariant::fromValue<qulonglong>(total_changed_pixels)},
        {QStringLiteral("layersTopFirst"), final_layers}, {QStringLiteral("toolReports"), reports},
        {QStringLiteral("modelPlan"), QVariantMap{{QStringLiteral("decision"), plan.decision},
            {QStringLiteral("toolCalls"), plan.tool_call_plan}, {QStringLiteral("promptAudit"), plan.prompt_audit},
            {QStringLiteral("elapsedMs"), planning_elapsed_ms}}},
        {QStringLiteral("creativeFollowUpPlan"), QVariantMap{{QStringLiteral("decision"), typography_plan.decision},
            {QStringLiteral("toolCalls"), typography_plan.tool_call_plan},
            {QStringLiteral("promptAudit"), typography_plan.prompt_audit},
            {QStringLiteral("elapsedMs"), typography_elapsed_ms}}},
        {QStringLiteral("finalRenderSha256"), hash(output_final)}, {QStringLiteral("recipeSha256"), hash(output_recipe)},
        {QStringLiteral("recipeReopenPixelIdentical"), true}, {QStringLiteral("finalRecipeSizeBytes"), QFileInfo(output_recipe).size()}};
    for (auto it = source_evidence.cbegin(); it != source_evidence.cend(); ++it) receipt.insert(it.key(), it.value());
    QFile receipt_file(receipt_path);
    if (!receipt_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        receipt_file.write(QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Indented)) <= 0)
      return fail("Persist the A5 MCP trace, photo render, recipe, and source evidence");
    if (!journal.finish(receipt_path)) return fail("Finalize the append-safe A5 run journal");
    bridge.stop();
    std::cout << "production_bridge_A5_glass_lettering=passed changed_pixels=" << total_changed_pixels
              << " material_pixels=" << material_changed_pixels
              << " receipt=" << receipt_file.fileName().toStdString() << '\n';
    return EXIT_SUCCESS;
  }
  if (is_a2) {
    const auto follow_up_calls = typography_plan.tool_call_plan;
    const auto typography_call = normalize_live_composition_tool_call(follow_up_calls.at(0));
    const auto effect_call = normalize_live_composition_tool_call(follow_up_calls.at(1));
    const auto paint_call = normalize_live_composition_tool_call(follow_up_calls.at(2));
    if (typography_call.isEmpty() || effect_call.isEmpty() || paint_call.isEmpty())
      return fail("Every A2 edit must use the registered composition tool with typed arguments");
    const auto typography_arguments = typography_call.value(QStringLiteral("arguments")).toMap();
    const auto effect_arguments = effect_call.value(QStringLiteral("arguments")).toMap();
    const auto paint_arguments = paint_call.value(QStringLiteral("arguments")).toMap();
    const auto typography_values = typography_arguments.value(QStringLiteral("values")).toMap();
    const auto effect_values = effect_arguments.value(QStringLiteral("values")).toMap();
    const auto paint_values = paint_arguments.value(QStringLiteral("values")).toMap();
    const auto axes = typography_values.value(QStringLiteral("axes")).toMap();
    const auto axis_key = a2_width_axis.value(QStringLiteral("tag")).toString();
    const auto width_value = axes.value(axis_key).toDouble();
    const auto seed_type = effect_values.value(QStringLiteral("seed")).metaType().id();
    const auto annotation_seed_type = paint_values.value(QStringLiteral("textureSeed")).metaType().id();
    const auto valid_integer = [](int type) {
      return type == QMetaType::Int || type == QMetaType::UInt ||
          type == QMetaType::LongLong || type == QMetaType::ULongLong;
    };
    const auto annotation_points = paint_values.value(QStringLiteral("points")).toList();
    const auto width_minimum = a2_width_axis.value(QStringLiteral("minimum")).toDouble();
    const auto width_maximum = a2_width_axis.value(QStringLiteral("maximum")).toDouble();
    const auto width_default = a2_width_axis.value(QStringLiteral("default")).toDouble();
    const auto registration_red = effect_values.value(QStringLiteral("redRegistrationX")).toDouble();
    const auto registration_blue = effect_values.value(QStringLiteral("blueRegistrationX")).toDouble();
    if (typography_call.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        typography_arguments.value(QStringLiteral("operation")).toString() != QStringLiteral("typography") ||
        typography_arguments.value(QStringLiteral("layerId")).toString() != text_layer_id ||
        typography_values.value(QStringLiteral("family")).toString() != QStringLiteral("Skia") ||
        typography_values.value(QStringLiteral("size")).toDouble() < 150.0 ||
        typography_values.value(QStringLiteral("size")).toDouble() > 220.0 ||
        !axes.contains(axis_key) || !std::isfinite(width_value) || width_value < width_minimum ||
        width_value > width_maximum || std::abs(width_value - width_default) < (width_maximum - width_minimum) * 0.05 ||
        effect_call.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        effect_arguments.value(QStringLiteral("operation")).toString() != QStringLiteral("add_effect") ||
        effect_arguments.value(QStringLiteral("layerId")).toString() != a2_source_layer_id ||
        effect_values.value(QStringLiteral("effectType")).toString() != QStringLiteral("imperfect_print") ||
        !std::isfinite(registration_red) || !std::isfinite(registration_blue) ||
        std::abs(registration_red) < 0.25 || std::abs(registration_red) > 0.9 ||
        std::abs(registration_blue) < 0.25 || std::abs(registration_blue) > 0.9 ||
        registration_red * registration_blue >= 0.0 ||
        effect_values.value(QStringLiteral("paperTexture")).toDouble() < 0.1 ||
        effect_values.value(QStringLiteral("paperTexture")).toDouble() > 0.35 ||
        effect_values.value(QStringLiteral("inkSpread")).toDouble() > 0.12 ||
        !valid_integer(seed_type) || effect_values.value(QStringLiteral("seed")).toULongLong() > 4294967295ULL ||
        paint_call.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        paint_arguments.value(QStringLiteral("operation")).toString() != QStringLiteral("paint") ||
        paint_arguments.value(QStringLiteral("layerId")).toString() != paint_layer_id ||
        paint_values.value(QStringLiteral("textureMaterial")).toString() != QStringLiteral("dry_ink") ||
        !valid_integer(annotation_seed_type) ||
        paint_values.value(QStringLiteral("textureSeed")).toULongLong() > 4294967295ULL ||
        annotation_points.size() < 3) {
      std::cerr << "A2 model plan rejected: "
                << QJsonDocument::fromVariant(QVariantMap{{QStringLiteral("calls"), follow_up_calls},
                     {QStringLiteral("widthAxis"), a2_width_axis},
                     {QStringLiteral("sourceLayerId"), a2_source_layer_id},
                     {QStringLiteral("textLayerId"), text_layer_id},
                     {QStringLiteral("paintLayerId"), paint_layer_id}})
                     .toJson(QJsonDocument::Compact).toStdString() << '\n';
      return fail("A2 must use the real non-default width axis and explicitly seeded typed print and dry-ink tools on returned layer IDs");
    }
    for (const auto& point_value : annotation_points) {
      const auto point = point_value.toMap();
      const auto x = point.value(QStringLiteral("x")).toDouble();
      const auto y = point.value(QStringLiteral("y")).toDouble();
      if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 ||
          x >= controller.sourceImageWidth() || y >= controller.sourceImageHeight())
        return fail("A2 brush annotation points must be finite canvas coordinates inside the photograph");
    }
    QString effect_id;
    for (const auto& call : follow_up_calls) {
      const auto model_call = normalize_live_composition_tool_call(call);
      if (model_call.isEmpty()) return fail("A2 follow-up calls must use the registered composition tool");
      const auto arguments = model_call.value(QStringLiteral("arguments")).toMap();
      const auto result = client.callTool(model_call.value(QStringLiteral("name")).toString(), arguments);
      if (!result.value(QStringLiteral("ok")).toBool()) {
        std::cerr << "A2 MCP dispatch failed: "
                  << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).toStdString()
                  << '\n';
        return fail("Execute all three model-selected print-cover operations through the production MCP bridge");
      }
      reports.append(QVariantMap{{QStringLiteral("name"), model_call.value(QStringLiteral("name"))},
          {QStringLiteral("arguments"), arguments}, {QStringLiteral("modelOutput"), model_call.value(QStringLiteral("modelOutput"))},
          {QStringLiteral("result"), result}});
      request.prior_tool_results.append(QVariantMap{{QStringLiteral("name"), model_call.value(QStringLiteral("name"))},
          {QStringLiteral("arguments"), arguments}, {QStringLiteral("result"), result}});
      if (arguments.value(QStringLiteral("operation")).toString() == QStringLiteral("add_effect"))
        effect_id = result.value(QStringLiteral("structuredContent")).toMap()
            .value(QStringLiteral("effectId")).toString();
      if (!journal.record(QStringLiteral("print_cover_operations_dispatched"), QVariantMap{
              {QStringLiteral("toolReports"), reports}}))
        return fail("Persist every completed model-selected print-cover operation");
    }
    if (effect_id.isEmpty()) return fail("The print effect must return its stable editable ID");
    QElapsedTimer paint_timer;
    paint_timer.start();
    while (controller.photoPaintBusy() && paint_timer.elapsed() < 60000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      QThread::msleep(5);
    }
    if (controller.photoPaintBusy() || !controller.photoPaintError().isEmpty())
      return fail("Wait for the drypoint layer raster commit and confirm the paint error is empty");
    const auto final_inspection = client.callTool(QStringLiteral("photo.inspect_composition"),
        {{QStringLiteral("assetId"), QStringLiteral("composition")}});
    if (!final_inspection.value(QStringLiteral("ok")).toBool())
      return fail("Inspect the final print cover through production MCP");
    const auto final_layers = final_inspection.value(QStringLiteral("structuredContent")).toMap()
        .value(QStringLiteral("layers")).toList();
    auto found_text = false;
    auto found_paint = false;
    auto found_source = false;
    for (const auto& layer_value : final_layers) {
      const auto layer = layer_value.toMap();
      const auto id = layer.value(QStringLiteral("id")).toString();
      if (id == text_layer_id) {
        const auto style = layer.value(QStringLiteral("textTypography")).toMap();
        const auto stored_axes = style.value(QStringLiteral("axes")).toMap();
        found_text = layer.value(QStringLiteral("kind")).toString() == QStringLiteral("text") &&
            layer.value(QStringLiteral("text")).toString() == QStringLiteral("WEATHER / MEMORY") &&
            stored_axes.contains(axis_key) &&
            std::abs(stored_axes.value(axis_key).toDouble() - width_value) < 1e-6;
      } else if (id == paint_layer_id) {
        found_paint = layer.value(QStringLiteral("kind")).toString() == QStringLiteral("pixel");
      } else if (id == a2_source_layer_id) {
        found_source = layer.value(QStringLiteral("kind")).toString() == QStringLiteral("source");
      }
    }
    auto state = EditorPreviewControllerTestAccess::state(controller);
    for (const auto& layer_value : state.layers) {
      const auto layer = layer_value.toMap();
      if (layer.value(QStringLiteral("id")).toString() == paint_layer_id) {
        found_paint = found_paint && layer.value(QStringLiteral("kind")).toString() == QStringLiteral("pixel") &&
            !layer.value(QStringLiteral("paintTiles")).toMap().isEmpty();
        break;
      }
    }
    const auto source_layer_index = [&]() {
      for (qsizetype i = 0; i < state.layers.size(); ++i)
        if (state.layers.at(i).toMap().value(QStringLiteral("id")).toString() == a2_source_layer_id)
          return static_cast<int>(i);
      return -1;
    }();
    const auto effects = source_layer_index >= 0 ? controller.photoLayerEffects(source_layer_index) : QVariantList{};
    auto found_effect = false;
    for (const auto& effect : effects) {
      const auto item = effect.toMap();
      found_effect = found_effect || (item.value(QStringLiteral("id")).toString() == effect_id &&
          item.value(QStringLiteral("type")).toString() == QStringLiteral("imperfect_print"));
    }
    if (!journal.record(QStringLiteral("reopened_a2_state_validated"), QVariantMap{
            {QStringLiteral("finalInspection"), final_inspection},
            {QStringLiteral("foundText"), found_text},
            {QStringLiteral("foundPaint"), found_paint},
            {QStringLiteral("foundSource"), found_source},
            {QStringLiteral("foundEffect"), found_effect},
            {QStringLiteral("expectedTextLayerId"), text_layer_id},
            {QStringLiteral("expectedPaintLayerId"), paint_layer_id},
            {QStringLiteral("expectedSourceLayerId"), a2_source_layer_id},
            {QStringLiteral("expectedEffectId"), effect_id},
            {QStringLiteral("paintTilesPresentInRecipe"), found_paint},
            {QStringLiteral("sourceLayerEffects"), effects}}))
      return fail("Persist reopened A2 validation diagnostics");
    if (!found_text || !found_paint || !found_source || !found_effect)
      return fail("The reopened A2 state must retain editable title, paint layer, and registered print effect");
    const auto final_render = render_output_image_with_backend(source, state, QColorSpace(QColorSpace::SRgb),
        QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    const auto recipe = serialize_edit_recipe_json(state);
    const auto reopened = render_output_image_with_backend(source, parse_edit_recipe_json(recipe),
        QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    if (final_render.isNull() || reopened != final_render)
      return fail("Render the finished A2 cover and prove exact saved-recipe reopen");
    auto without_paint_state = state;
    for (auto& layer_value : without_paint_state.layers) {
      auto layer = layer_value.toMap();
      if (layer.value(QStringLiteral("id")).toString() == paint_layer_id) {
        layer_value = QVariantMap{{QStringLiteral("id"), paint_layer_id},
            {QStringLiteral("kind"), QStringLiteral("pixel")},
            {QStringLiteral("pixelWidth"), source.width()}, {QStringLiteral("pixelHeight"), source.height()},
            {QStringLiteral("canvasWidth"), source.width()}, {QStringLiteral("canvasHeight"), source.height()},
            {QStringLiteral("paintTiles"), QVariantMap{}}, {QStringLiteral("enabled"), true},
            {QStringLiteral("opacity"), 1.0}, {QStringLiteral("scale"), 1.0}};
      }
    }
    const auto without_paint = render_output_image_with_backend(source, without_paint_state,
        QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    std::uint64_t annotation_pixels = 0;
    std::uint64_t changed_pixels = 0;
    if (!without_paint.isNull()) {
      for (int y = 0; y < source.height(); ++y) for (int x = 0; x < source.width(); ++x) {
        if (final_render.pixel(x, y) != without_paint.pixel(x, y)) ++annotation_pixels;
        if (final_render.pixel(x, y) != source.pixel(x, y)) ++changed_pixels;
      }
    }
    if (annotation_pixels < 8 || changed_pixels < 1000)
      return fail("A2 print and annotation must make measurable rendered changes while preserving editable layers");
    const auto hash = [](const QString& path) {
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly)) return QString{};
      return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
    };
    const auto output_final = QDir(receipt_dir).filePath(QStringLiteral("attempt-final-render.png"));
    const auto output_base = QDir(receipt_dir).filePath(QStringLiteral("attempt-base-render.png"));
    const auto output_recipe = QDir(receipt_dir).filePath(QStringLiteral("final-recipe.json"));
    auto base_state = state;
    base_state.layers.clear();
    const auto base_render = render_output_image_with_backend(source, base_state,
        QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    if (base_render.isNull() || !base_render.save(output_base) || !final_render.save(output_final))
      return fail("Save source and final render evidence for the A2 production-bridge run");
    QFile recipe_file(output_recipe);
    if (!recipe_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        recipe_file.write(recipe.toUtf8()) != recipe.toUtf8().size())
      return fail("Save the editable A2 recipe");
    recipe_file.close();
    const auto source_evidence = currentCreativeRunSourceEvidence();
    if (source_evidence.value(QStringLiteral("sourceRevision")).toString().isEmpty() ||
        source_evidence.value(QStringLiteral("sourceEvidenceSha256")).toString().isEmpty() ||
        !journal.record(QStringLiteral("source_evidence_captured"), source_evidence))
      return fail("Capture source revision and scoped dirty-worktree evidence for A2");
    const auto axis_receipt = QVariantMap{{QStringLiteral("family"), QStringLiteral("Skia")},
        {QStringLiteral("tag"), axis_key}, {QStringLiteral("minimum"), width_minimum},
        {QStringLiteral("maximum"), width_maximum}, {QStringLiteral("default"), width_default},
        {QStringLiteral("value"), width_value}};
    QVariantMap receipt{{QStringLiteral("contract"), QStringLiteral("production-bridge-real-photo-creative-run-v1")},
        {QStringLiteral("briefId"), brief_id}, {QStringLiteral("attempt"), attempt},
        {QStringLiteral("modelId"), model_id}, {QStringLiteral("modelFamily"), probe.value(QStringLiteral("modelFamily"))},
        {QStringLiteral("ollamaModel"), probe.value(QStringLiteral("ollamaModel"))},
        {QStringLiteral("creativeBrief"), brief}, {QStringLiteral("phaseInstructions"), phase_one_instructions},
        {QStringLiteral("localOnly"), true}, {QStringLiteral("cloudUsed"), false},
        {QStringLiteral("automaticCommitAllowed"), false}, {QStringLiteral("productionBridge"), true},
        {QStringLiteral("endpointKind"), bridge.mcpEndpointKind()}, {QStringLiteral("realPhotoInput"), true},
        {QStringLiteral("countsTowardPlan18Runs"), true}, {QStringLiteral("sourceAssetId"), QStringLiteral("architecture-academic-building")},
        {QStringLiteral("sourcePath"), source_path}, {QStringLiteral("sourceSha256"), hash(source_path)},
        {QStringLiteral("modelPlan"), QVariantMap{{QStringLiteral("decision"), plan.decision},
            {QStringLiteral("toolCalls"), plan.tool_call_plan}, {QStringLiteral("promptAudit"), plan.prompt_audit},
            {QStringLiteral("elapsedMs"), planning_elapsed_ms}}},
        {QStringLiteral("printCoverLayerPlan"), a2_layer_creation_plan},
        {QStringLiteral("creativeFollowUpPlan"), QVariantMap{{QStringLiteral("decision"), typography_plan.decision},
            {QStringLiteral("toolCalls"), follow_up_calls}, {QStringLiteral("promptAudit"), typography_plan.prompt_audit},
            {QStringLiteral("elapsedMs"), typography_elapsed_ms}}},
        {QStringLiteral("toolReports"), reports}, {QStringLiteral("layersTopFirst"), final_layers},
        {QStringLiteral("variableFontAxis"), axis_receipt}, {QStringLiteral("printEffectId"), effect_id},
        {QStringLiteral("printEffectSeed"), effect_values.value(QStringLiteral("seed"))},
        {QStringLiteral("paintLayerId"), paint_layer_id}, {QStringLiteral("annotationSeed"), paint_values.value(QStringLiteral("textureSeed"))},
        {QStringLiteral("annotationChangedPixels"), QVariant::fromValue<qulonglong>(annotation_pixels)},
        {QStringLiteral("totalChangedPixels"), QVariant::fromValue<qulonglong>(changed_pixels)},
        {QStringLiteral("finalRenderSha256"), hash(output_final)}, {QStringLiteral("recipeSha256"), hash(output_recipe)},
        {QStringLiteral("finalRecipeSizeBytes"), QFileInfo(output_recipe).size()},
        {QStringLiteral("recipeReopenPixelIdentical"), true}, {QStringLiteral("paintError"), controller.photoPaintError()}};
    for (auto it = source_evidence.cbegin(); it != source_evidence.cend(); ++it) receipt.insert(it.key(), it.value());
    QFile receipt_file(receipt_path);
    if (!receipt_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        receipt_file.write(QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Indented)) <= 0)
      return fail("Persist the A2 MCP trace, render, recipe, and qualification evidence");
    if (!journal.finish(receipt_path)) return fail("Finalize the A2 append-safe run-status journal");
    bridge.stop();
    std::cout << "production_bridge_A2_print_cover=passed changed_pixels=" << changed_pixels
              << " annotation_pixels=" << annotation_pixels
              << " receipt=" << receipt_file.fileName().toStdString() << '\n';
    return EXIT_SUCCESS;
  }
  const auto typography_call = normalize_live_composition_tool_call(typography_plan.tool_call_plan.front());
  if (typography_call.isEmpty()) return fail("The typography follow-up must use the registered composition tool");
  const auto typography_arguments = typography_call.value(QStringLiteral("arguments")).toMap();
  if (typography_call.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
      typography_arguments.value(QStringLiteral("operation")).toString() != QStringLiteral("typography") ||
      typography_arguments.value(QStringLiteral("layerId")).toString() != text_layer_id)
    return fail("The model follow-up must target the exact text layer returned by production MCP");
  const auto typography_result = client.callTool(
      typography_call.value(QStringLiteral("name")).toString(), typography_arguments);
  if (!typography_result.value(QStringLiteral("ok")).toBool())
    return fail("Apply the model-selected typography operation through the production MCP bridge");
  reports.append(QVariantMap{{QStringLiteral("name"), typography_call.value(QStringLiteral("name"))},
      {QStringLiteral("arguments"), typography_arguments}, {QStringLiteral("result"), typography_result}});
  if (!journal.record(QStringLiteral("typography_dispatched"), QVariantMap{
          {QStringLiteral("toolReports"), reports}}))
    return fail("Persist the completed model-selected typography operation");
  auto text_only_state = EditorPreviewControllerTestAccess::state(controller);
  if (text_only_state.layers.size() < 2 ||
      text_only_state.layers.front().toMap().value(QStringLiteral("kind")).toString() != QStringLiteral("pixel"))
    return fail("The cutout must remain the top layer while the text receives its follow-up style");
  text_only_state.layers.removeFirst();
  text_only = render_output_image_with_backend(source, text_only_state,
      QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);

  QElapsedTimer paint_timer;
  paint_timer.start();
  while (controller.photoPaintBusy() && paint_timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(5);
  }
  if (controller.photoPaintBusy() || !controller.photoPaintError().isEmpty())
    return fail("Wait for successful subject-cutout materialization in the production controller");

  const auto inspect = client.callTool(QStringLiteral("photo.inspect_composition"),
      {{QStringLiteral("assetId"), QStringLiteral("composition")}});
  if (!inspect.value(QStringLiteral("ok")).toBool()) return fail("Inspect the production composition over MCP");
  const auto layers = inspect.value(QStringLiteral("structuredContent")).toMap()
      .value(QStringLiteral("layers")).toList();
  if (layers.size() < 2 || layers.at(0).toMap().value(QStringLiteral("kind")).toString() != QStringLiteral("pixel") ||
      layers.at(1).toMap().value(QStringLiteral("id")).toString() != text_layer_id)
    return fail("The independent selection copy must be above the editable text layer");
  const auto state = EditorPreviewControllerTestAccess::state(controller);
  const auto final_render = render_output_image_with_backend(source, state, QColorSpace(QColorSpace::SRgb),
      QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
  auto base_state = state;
  base_state.layers.clear();
  const auto base_render = render_output_image_with_backend(source, base_state,
      QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
  const auto recipe = serialize_edit_recipe_json(state);
  const auto reopened = render_output_image_with_backend(source, parse_edit_recipe_json(recipe),
      QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
  if (text_only.isNull() || final_render.isNull() || base_render.isNull() || reopened != final_render)
    return fail("Render the final composition and prove exact serialized-recipe reopen");
  if (!QDir().mkpath(receipt_dir) ||
      !text_only.save(QDir(receipt_dir).filePath(QStringLiteral("attempt-text-over-photo.png"))) ||
      !base_render.save(QDir(receipt_dir).filePath(QStringLiteral("attempt-base-render.png"))) ||
      !final_render.save(QDir(receipt_dir).filePath(QStringLiteral("attempt-final-render.png"))))
    return fail("Save the three diagnostic renders for the completed real-photo composition attempt");
  std::uint64_t occluded_pixels = 0;
  std::uint64_t changed_headline_pixels = 0;
  for (int y = 0; y < source.height(); ++y) {
    const auto* mask_row = gray.constScanLine(y);
    for (int x = 0; x < source.width(); ++x) {
      if (text_only.pixel(x, y) == base_render.pixel(x, y)) continue;
      if (final_render.pixel(x, y) != text_only.pixel(x, y)) ++changed_headline_pixels;
      if (mask_row[x] >= 240 && final_render.pixel(x, y) == base_render.pixel(x, y)) ++occluded_pixels;
    }
  }
  if (changed_headline_pixels < 1000) {
    QVariantList compact_layers;
    for (const auto& value : layers) {
      const auto layer = value.toMap();
      compact_layers.append(QVariantMap{
          {QStringLiteral("id"), layer.value(QStringLiteral("id"))},
          {QStringLiteral("kind"), layer.value(QStringLiteral("kind"))},
          {QStringLiteral("name"), layer.value(QStringLiteral("name"))},
          {QStringLiteral("position"), layer.value(QStringLiteral("position"))},
          {QStringLiteral("x"), layer.value(QStringLiteral("x"))},
          {QStringLiteral("y"), layer.value(QStringLiteral("y"))}});
    }
    std::cerr << "composition occlusion diagnostic: " << QJsonDocument::fromVariant(QVariantMap{
        {QStringLiteral("occludedPixels"), QVariant::fromValue<qulonglong>(occluded_pixels)},
        {QStringLiteral("changedHeadlinePixels"), QVariant::fromValue<qulonglong>(changed_headline_pixels)},
        {QStringLiteral("layerOrder"), compact_layers}, {QStringLiteral("toolCalls"), plan.tool_call_plan},
        {QStringLiteral("textLayerId"), text_layer_id},
        {QStringLiteral("textLayerCount"), state.layers.size()}})
        .toJson(QJsonDocument::Compact).toStdString() << '\n';
    return fail("The selected foreground must visibly alter at least 1,000 headline pixels");
  }

  const auto output_text = QDir(receipt_dir).filePath(QStringLiteral("text-over-photo.png"));
  const auto output_final = QDir(receipt_dir).filePath(QStringLiteral("text-behind-subject.png"));
  const auto output_recipe = QDir(receipt_dir).filePath(QStringLiteral("final-recipe.json"));
  if (!text_only.save(output_text) || !final_render.save(output_final))
    return fail("Save before/after renders from the live production-bridge run");
  QFile recipe_file(output_recipe);
  if (!recipe_file.open(QIODevice::WriteOnly | QIODevice::Truncate) || recipe_file.write(recipe.toUtf8()) <= 0)
    return fail("Save the editable final composition recipe");
  recipe_file.close();
  const auto hash = [](const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QString{};
    return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
  };
  const auto source_evidence = currentCreativeRunSourceEvidence();
  if (source_evidence.value(QStringLiteral("sourceRevision")).toString().isEmpty() ||
      source_evidence.value(QStringLiteral("sourceEvidenceSha256")).toString().isEmpty())
    return fail("Capture the source revision and scoped dirty-worktree evidence for the model run");
  if (!journal.record(QStringLiteral("source_evidence_captured"), source_evidence))
    return fail("Persist source revision and scoped source evidence into the attempt journal");
  QVariantMap receipt{
      {QStringLiteral("contract"), QStringLiteral("production-bridge-real-photo-creative-run-v1")},
      {QStringLiteral("briefId"), brief_id}, {QStringLiteral("attempt"), attempt},
      {QStringLiteral("modelId"), model_id}, {QStringLiteral("modelFamily"), probe.value(QStringLiteral("modelFamily"))},
      {QStringLiteral("ollamaModel"), probe.value(QStringLiteral("ollamaModel"))},
      {QStringLiteral("creativeBrief"), brief}, {QStringLiteral("localOnly"), true},
      {QStringLiteral("cloudUsed"), false}, {QStringLiteral("automaticCommitAllowed"), false},
      {QStringLiteral("endpointKind"), bridge.mcpEndpointKind()},
      {QStringLiteral("productionBridge"), true}, {QStringLiteral("realPhotoInput"), true},
      {QStringLiteral("countsTowardPlan18Runs"), true},
      {QStringLiteral("sourcePath"), source_path}, {QStringLiteral("sourceSha256"), hash(source_path)},
      {QStringLiteral("maskPath"), mask_path}, {QStringLiteral("maskSha256"), hash(mask_path)},
      {QStringLiteral("modelPlan"), QVariantMap{
          {QStringLiteral("decision"), plan.decision}, {QStringLiteral("summary"), plan.plan_summary},
          {QStringLiteral("toolCalls"), plan.tool_call_plan}, {QStringLiteral("promptAudit"), plan.prompt_audit},
          {QStringLiteral("elapsedMs"), planning_elapsed_ms}}},
      {QStringLiteral("typographyFollowUpPlan"), QVariantMap{
          {QStringLiteral("decision"), typography_plan.decision},
          {QStringLiteral("toolCalls"), typography_plan.tool_call_plan},
          {QStringLiteral("promptAudit"), typography_plan.prompt_audit},
          {QStringLiteral("elapsedMs"), typography_elapsed_ms}}},
      {QStringLiteral("toolReports"), reports}, {QStringLiteral("layersTopFirst"), layers},
      {QStringLiteral("occludedHeadlinePixels"), QVariant::fromValue<qulonglong>(occluded_pixels)},
      {QStringLiteral("subjectAffectedHeadlinePixels"), QVariant::fromValue<qulonglong>(changed_headline_pixels)},
      {QStringLiteral("textRenderSha256"), hash(output_text)}, {QStringLiteral("finalRenderSha256"), hash(output_final)},
      {QStringLiteral("recipeSha256"), hash(output_recipe)},
      {QStringLiteral("finalRecipeSizeBytes"), QFileInfo(output_recipe).size()},
      {QStringLiteral("recipeReopenPixelIdentical"), true}};
  for (auto it = source_evidence.cbegin(); it != source_evidence.cend(); ++it)
    receipt.insert(it.key(), it.value());
  QFile receipt_file(receipt_path);
  if (!receipt_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      receipt_file.write(QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Indented)) <= 0)
    return fail("Persist the model plan, MCP trace, renders, and exact recipe-reopen receipt");
  if (!journal.finish(receipt_path))
    return fail("Finalize the append-safe creative-run status journal");
  bridge.stop();
  std::cout << "production_bridge_real_photo_creative_run=passed"
            << " occluded_headline_pixels=" << occluded_pixels
            << " receipt=" << receipt_file.fileName().toStdString() << '\n';
  return EXIT_SUCCESS;
}
}  // namespace

#include "agent_thumbnail_live_proof.inc"
#include "agent_design_sample.inc"

#include "agent_multi_document_contract.h"

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_DESIGN_SAMPLE_DIR"))
    return run_agent_design_sample();
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_THUMBNAIL_PROOF_DIR"))
    return run_agent_thumbnail_live_proof();
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_RUN_PRODUCTION_BRIDGE_COMPOSITION"))
    return run_production_bridge_live_composition();
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QTemporaryDir temp;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", temp.path().toUtf8());
  CHECK(AppBootstrap::ensureRuntimeDirectories(), "Runtime directories");
  QImage source(480, 320, QImage::Format_RGBA8888);
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  source.fill(QColor("#405060"));
  const auto review_source_path = qEnvironmentVariable("PHOTO_COMPOSITION_REVIEW_SOURCE");
  const auto review_mask_path = qEnvironmentVariable("PHOTO_COMPOSITION_REVIEW_MASK");
  const auto real_photo_review = !review_source_path.isEmpty();
  if (real_photo_review) {
    source = QImage(review_source_path);
    CHECK(!source.isNull(), "Real composition review source must load");
    source.setColorSpace(QColorSpace::SRgb);
  }
  const auto render = [&](const EditRecipeState& state) {
    return render_output_image_with_backend(source, state, QColorSpace(QColorSpace::SRgb),
        QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
  };
  QImage transparent_source(source.size(), QImage::Format_RGBA8888);
  transparent_source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  transparent_source.fill(Qt::transparent);
  const auto render_transparent = [&](const EditRecipeState& render_state) {
    return render_output_image_with_backend(transparent_source, render_state, QColorSpace(QColorSpace::SRgb),
        QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
  };
  if (real_photo_review) {
    CHECK(!review_mask_path.isEmpty(), "Real composition review requires a prompted SAM2 mask");
    const QImage decoded_mask(review_mask_path);
    CHECK(!decoded_mask.isNull() && decoded_mask.size() == source.size(),
        "Prompted mask must match the real photo's full source dimensions");
    const auto gray_mask = decoded_mask.convertToFormat(QImage::Format_Grayscale8);
    QByteArray selection_alpha(source.width() * source.height(), char(0));
    for (int y = 0; y < source.height(); ++y) {
      const auto* row = gray_mask.constScanLine(y);
      for (int x = 0; x < source.width(); ++x)
        selection_alpha[y * source.width() + x] = static_cast<char>(row[x]);
    }
    EditRecipeState initial_state;
    initial_state.mask_stack = {QVariantMap{
        {"id", "sam2-subject"}, {"maskSetId", "sam2-subject-selection"},
        {"maskSetName", "Prompted subject"}, {"toolRole", "selection"},
        {"kind", "brush"}, {"enabled", true}, {"operation", "add"},
        {"segmentationWidth", source.width()}, {"segmentationHeight", source.height()},
        {"segmentationData", selection_alpha}}};
    initial_state.active_mask_index = 0;
    const auto source_path = temp.filePath("real-subject-source.png");
    CHECK(source.save(source_path), "Save the exact real-photo source used by the workflow");
    EditorPreviewImageProvider provider;
    auto controller = std::make_unique<EditorPreviewController>();
    controller->setImageProvider(&provider);
    controller->loadAsset("real-subject-composition", source_path, {}, {}, {}, {},
        serialize_edit_recipe_json(initial_state));
    QElapsedTimer timer;
    timer.start();
    while (controller->displayPreviewSource().isEmpty() && timer.elapsed() < 60000) {
      QCoreApplication::processEvents();
      QThread::msleep(5);
    }
    CHECK(!controller->displayPreviewSource().isEmpty(), "Real-photo preview must load");
    CHECK(controller->selectMaskSet(QStringLiteral("sam2-subject-selection")),
        "Select the prompted subject mask in the production controller");
    const auto command = [&](const QString& operation, const QVariantMap& values = {},
                             const QString& layer_id = {}) {
      return controller->photoCompositionCommand(QJsonObject::fromVariantMap({
          {"assetId", "real-subject-composition"}, {"operation", operation},
          {"values", values}, {"layerId", layer_id}}).toVariantMap());
    };
    CHECK(command("add_text", {{"text", "THE SHAPE OF A MOMENT"},
        {"x", 70}, {"y", 600}}).value("ok").toBool(),
        "Agent composition command creates the text over the real portrait");
    const auto text_layer_id = controller->photoLayers().front().toMap()
        .value("id").toString();
    CHECK(!text_layer_id.isEmpty() && command("typography", {
        {"family", "Georgia"}, {"size", 160.0}, {"weight", 700},
        {"color", "#f2e7cd"}, {"strokeWidth", 3.0},
        {"strokeColor", "#17232b"}, {"shadowX", 3.0}, {"shadowY", 5.0}},
        text_layer_id).value("ok").toBool(),
        "Typed typography command styles the real-photo headline");
    const auto text_state = EditorPreviewControllerTestAccess::state(*controller);
    const auto text_only = render(text_state);
    CHECK(!text_only.isNull(), "Styled text should render over the real photograph");
    const auto mask_before_refinement = controller->maskStack();
    const auto alpha_before_refinement = mask_before_refinement.front().toMap()
        .value("segmentationData").toByteArray();
    controller->setActiveMaskBrushSize(0.035);
    controller->setActiveMaskBrushFlow(1.0);
    controller->setActiveMaskBrushHardness(1.0);
    controller->setActiveMaskBrushSpacing(0.10);
    controller->setActiveMaskBrushSmoothing(0.0);
    controller->setActiveMaskBrushBuildup(false);
    controller->setActiveMaskBrushEraseMode(false);
    controller->beginActiveBrushStroke();
    for (int step = 0; step <= 6; ++step)
      controller->addBrushPoint(0.34 + step * 0.053, 0.18);
    for (int step = 6; step >= 0; --step)
      controller->addBrushPoint(0.34 + step * 0.053, 0.20);
    for (int step = 0; step <= 6; ++step)
      controller->addBrushPoint(0.34 + step * 0.053, 0.22);
    controller->endActiveBrushStroke();
    QElapsedTimer refinement_timer;
    refinement_timer.start();
    while (EditorPreviewControllerTestAccess::pending_brush_commit_count(*controller) > 0 &&
           refinement_timer.elapsed() < 60000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      QThread::msleep(5);
    }
    const auto refined_mask = controller->maskStack();
    const auto alpha_after_refinement = refined_mask.front().toMap()
        .value("segmentationData").toByteArray();
    const auto stroke_center_x = static_cast<int>(std::lround(0.49 * (source.width() - 1)));
    const auto stroke_center_y = static_cast<int>(std::lround(0.20 * (source.height() - 1)));
    const auto stroke_center_index = stroke_center_y * source.width() + stroke_center_x;
    const auto stroke_center_alpha_before = static_cast<unsigned char>(
        alpha_before_refinement.at(stroke_center_index));
    const auto stroke_center_alpha_after = static_cast<unsigned char>(
        alpha_after_refinement.at(stroke_center_index));
    auto changed_alpha_pixels = std::uint64_t{0};
    for (auto index = 0; index < std::min(alpha_before_refinement.size(),
                                           alpha_after_refinement.size()); ++index) {
      if (alpha_before_refinement[index] != alpha_after_refinement[index])
        ++changed_alpha_pixels;
    }
    CHECK(EditorPreviewControllerTestAccess::pending_brush_commit_count(*controller) == 0 &&
        alpha_after_refinement.size() == alpha_before_refinement.size() &&
        changed_alpha_pixels > 0 && stroke_center_alpha_after == 255,
        "An additive brush refinement must finish its raster commit and make its stroke center opaque");
    std::cout << "brush_refinement_changed_alpha_pixels=" << changed_alpha_pixels
              << " stroke_center_alpha=" << static_cast<int>(stroke_center_alpha_before)
              << "->" << static_cast<int>(stroke_center_alpha_after) << '\n';
    const auto copy_result = command("copy_selection", {
        {"maskId", "sam2-subject-selection"}, {"independent", true}});
    CHECK(copy_result.value("ok").toBool() && copy_result.value("paintBusy").toBool(),
        "Queue an independent prompted-subject cutout above the text layer");
    QElapsedTimer materialize_timer;
    materialize_timer.start();
    while (controller->photoPaintBusy() && materialize_timer.elapsed() < 60000) {
      QCoreApplication::processEvents();
      QThread::msleep(5);
    }
    CHECK(!controller->photoPaintBusy() && controller->photoPaintError().isEmpty(),
        "Independent prompted-subject pixels materialize without an async error");
    const auto composition = EditorPreviewControllerTestAccess::state(*controller);
    const auto composite = render(composition);
    auto base_state = composition;
    base_state.layers.clear();
    const auto base = render(base_state);
    const auto debug_layer = composition.layers.isEmpty()
        ? QVariantMap{} : composition.layers.front().toMap();
    const auto debug_pixels = rasterize_photo_paint_layer(debug_layer);
    CHECK(!composite.isNull() && composite.size() == source.size(),
        "The masked subject composition renders at source resolution");
    std::uint64_t foreground_covered_text_pixels = 0;
    std::uint64_t visible_text_pixels = 0;
    std::uint64_t soft_mask_text_pixels = 0;
    std::uint64_t brush_band_text_pixels = 0;
    std::uint64_t brush_band_opaque_before = 0;
    std::uint64_t brush_band_opaque_after = 0;
    auto text_left = source.width();
    auto text_top = source.height();
    auto text_right = -1;
    auto text_bottom = -1;
    for (int y = 0; y < source.height(); ++y) {
      for (int x = 0; x < source.width(); ++x) {
        const auto refined_alpha = static_cast<unsigned char>(
            alpha_after_refinement.at(y * source.width() + x));
        if (text_only.pixel(x, y) == base.pixel(x, y)) continue;
        ++visible_text_pixels;
        const auto normalized_x = static_cast<double>(x) / (source.width() - 1);
        const auto normalized_y = static_cast<double>(y) / (source.height() - 1);
        if (normalized_x >= 0.30 && normalized_x <= 0.70 &&
            normalized_y >= 0.145 && normalized_y <= 0.255) {
          ++brush_band_text_pixels;
          const auto pixel_index = y * source.width() + x;
          if (static_cast<unsigned char>(alpha_before_refinement.at(pixel_index)) == 255)
            ++brush_band_opaque_before;
          if (refined_alpha == 255) ++brush_band_opaque_after;
        }
        text_left = std::min(text_left, x);
        text_top = std::min(text_top, y);
        text_right = std::max(text_right, x);
        text_bottom = std::max(text_bottom, y);
        if (refined_alpha > 0) ++soft_mask_text_pixels;
        const auto actual = composite.pixelColor(x, y);
        const auto original = base.pixelColor(x, y);
        const auto unoccluded = text_only.pixelColor(x, y);
        if (refined_alpha == 255)
          CHECK(actual == original,
              "Fully opaque subject pixels must cover text without changing foreground color");
        if (refined_alpha < 192) continue;
        const auto linear_channel = [](int value) {
          const auto encoded = static_cast<double>(value) / 255.0;
          return encoded <= 0.04045 ? encoded / 12.92
                                   : std::pow((encoded + 0.055) / 1.055, 2.4);
        };
        const auto distance_to_subject =
            std::abs(linear_channel(actual.red()) - linear_channel(original.red())) +
            std::abs(linear_channel(actual.green()) - linear_channel(original.green())) +
            std::abs(linear_channel(actual.blue()) - linear_channel(original.blue()));
        const auto distance_to_text =
            std::abs(linear_channel(actual.red()) - linear_channel(unoccluded.red())) +
            std::abs(linear_channel(actual.green()) - linear_channel(unoccluded.green())) +
            std::abs(linear_channel(actual.blue()) - linear_channel(unoccluded.blue()));
        const auto foreground_text_separation =
            std::abs(original.red() - unoccluded.red()) +
            std::abs(original.green() - unoccluded.green()) +
            std::abs(original.blue() - unoccluded.blue());
        if (foreground_text_separation <= 8) continue;
        if (distance_to_subject >= distance_to_text) {
          std::cerr << "composition blend mismatch x=" << x << " y=" << y
                    << " maskAlpha=" << static_cast<int>(refined_alpha)
                    << " distanceToSubject=" << distance_to_subject
                    << " distanceToText=" << distance_to_text
                    << " actual=" << actual.red() << ',' << actual.green() << ',' << actual.blue()
                    << " subject=" << original.red() << ',' << original.green() << ',' << original.blue()
                    << " text=" << unoccluded.red() << ',' << unoccluded.green() << ',' << unoccluded.blue()
                    << " topKind=" << debug_layer.value("kind").toString().toStdString()
                    << " topId=" << debug_layer.value("id").toString().toStdString()
                    << " topSelectionScoped=" << debug_layer.value("selectionScoped").toBool()
                    << " topPixel=";
          if (!debug_pixels.isNull() && debug_pixels.size() == source.size()) {
            const auto pixel = debug_pixels.pixelColor(x, y);
            std::cerr << pixel.red() << ',' << pixel.green() << ',' << pixel.blue()
                      << ',' << pixel.alpha();
          } else {
            std::cerr << "none";
          }
          std::cerr
                    << '\n';
          return 1;
        }
        ++foreground_covered_text_pixels;
      }
    }
    std::cout << "headline_bounds=" << text_left << ',' << text_top << '-'
              << text_right << ',' << text_bottom
              << " visible_text_pixels=" << visible_text_pixels
              << " soft_mask_text_pixels=" << soft_mask_text_pixels
              << " foreground_covered_text_pixels=" << foreground_covered_text_pixels
              << " brush_band_opaque_text_pixels=" << brush_band_opaque_before << "->"
              << brush_band_opaque_after << '/' << brush_band_text_pixels << '\n';
    CHECK(foreground_covered_text_pixels > 1000,
        "The real prompted mask must visibly occlude a meaningful part of the headline");
    CHECK(brush_band_opaque_after > brush_band_opaque_before,
        "Brush Add must increase fully opaque coverage where the headline crosses the face");
    const auto reopened = parse_edit_recipe_json(serialize_edit_recipe_json(composition));
    CHECK(validate_edit_recipe_json(serialize_edit_recipe_json(composition)).valid &&
        render(reopened) == composite,
        "The real-photo prompted-mask composition must survive exact recipe reopen");
    const auto evidence = qEnvironmentVariable("PHOTO_COMPOSITION_EVIDENCE");
    if (!evidence.isEmpty()) {
      CHECK(QDir().mkpath(evidence), "Create real-photo composition evidence directory");
      CHECK(text_only.save(evidence + "/text-over-source.png"), "Save text-before-cutout render");
      CHECK(composite.save(evidence + "/text-behind-prompted-subject.png"),
          "Save text-behind-subject composition");
    }
    std::cout << "real_photo_dimensions=" << source.width() << 'x' << source.height()
              << " foreground_covered_text_pixels=" << foreground_covered_text_pixels
              << " real_prompted_selection=1 exact_recipe_reopen=1\n"
              << "Production controller composed a real prompted subject cutout over styled text\n";
    return EXIT_SUCCESS;
  }
  const QVariantMap fill{{"id", "shape"}, {"kind", "fill"}, {"fillColor", "#806040"}};
  const QVariantMap adjustment{{"id", "adjust"}, {"kind", "adjustment"}, {"exposure", 2.0},
      {"maskWidth", 2}, {"maskHeight", 1}, {"maskData", QByteArray::fromHex("ff00")}};
  EditRecipeState state;
  state.layers = {fill};
  const auto reference = render(state);
  state.layers = {fill, adjustment};
  CHECK(render(state) == reference, "An adjustment below opaque content must not alter it");
  state.layers = {adjustment, fill};
  CHECK(render(state) != reference, "An adjustment above content must alter it");
  auto child_fill = fill; child_fill.insert("parentId", "group");
  auto child_adjustment = adjustment; child_adjustment.insert("parentId", "group");
  const QVariantMap group{{"id", "group"}, {"kind", "group"}};
  state.layers = {group, child_fill, child_adjustment};
  CHECK(render(state) == reference, "Group children must obey top-first order");
  state.layers = {group, child_adjustment, child_fill};
  CHECK(render(state) != reference, "Group adjustment must affect only children below it");
  auto untransformed_group = group;
  auto full_canvas_fill = fill;
  full_canvas_fill.insert("parentId", "group");
  state.layers = {untransformed_group, full_canvas_fill};
  const auto untransformed_group_image = render(state);
  auto translated_group = untransformed_group;
  translated_group.insert("x", 32);
  state.layers = {translated_group, full_canvas_fill};
  CHECK(render(state) != untransformed_group_image,
      "Group transform must affect the composited child surface");

  // Exercise the shared ordering gate as a fixed matrix rather than relying on
  // a few representative examples. For opaque pixels, an adjustment below
  // the layer must leave the rendered foreground exactly unchanged.
  const QColor matrix_color("#b03070");
  QByteArray foreground_mask(source.width() * source.height(), char(255));
  for (int y = 0; y < source.height(); ++y) {
    for (int x = source.width() / 2; x < source.width(); ++x)
      foreground_mask[y * source.width() + x] = char(0);
  }
  QByteArray adjustment_mask(source.width() * source.height(), char(255));
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width() / 3; ++x)
      adjustment_mask[y * source.width() + x] = char(0);
  }
  int ordering_fixtures = 0;
  for (int foreground_kind = 0; foreground_kind < 3; ++foreground_kind) {
    for (const double foreground_opacity : {1.0, 0.55}) {
      for (const bool transformed : {false, true}) {
        for (const bool masked_adjustment : {false, true}) {
          for (const bool foreground_visible : {true, false}) {
            auto foreground = fill;
            foreground.insert("fillColor", matrix_color.name(QColor::HexArgb));
            foreground.insert("opacity", foreground_opacity);
            foreground.insert("enabled", foreground_visible);
            if (foreground_kind == 1) {
              foreground.insert("maskData", foreground_mask);
              foreground.insert("maskWidth", source.width());
              foreground.insert("maskHeight", source.height());
            }
            const auto foreground_id = QStringLiteral("matrix-foreground-%1").arg(ordering_fixtures);
            foreground.insert("id", foreground_id);
            QVariantList foreground_stack;
            if (foreground_kind == 2) {
              auto matrix_group = group;
              matrix_group.insert("id", "matrix-group");
              if (transformed) {
                matrix_group.insert("x", 11);
                matrix_group.insert("y", 7);
                matrix_group.insert("scale", 1.08);
              }
              foreground.insert("parentId", "matrix-group");
              foreground_stack = {matrix_group, foreground};
            } else {
              if (transformed) {
                foreground.insert("x", 11);
                foreground.insert("y", 7);
                foreground.insert("scale", 1.08);
              }
              foreground_stack = {foreground};
            }
            auto matrix_adjustment = adjustment;
            matrix_adjustment.insert("id", "matrix-adjustment");
            if (masked_adjustment) {
              matrix_adjustment.insert("maskData", adjustment_mask);
              matrix_adjustment.insert("maskWidth", source.width());
              matrix_adjustment.insert("maskHeight", source.height());
            } else {
              matrix_adjustment.insert("maskData", QByteArray{});
              matrix_adjustment.insert("maskWidth", 0);
              matrix_adjustment.insert("maskHeight", 0);
            }
            auto foreground_only_state = EditRecipeState{};
            foreground_only_state.layers = foreground_stack;
            const auto foreground_only = render(foreground_only_state);
            const auto foreground_alpha = render_transparent(foreground_only_state);
            auto lower_adjustment_state = foreground_only_state;
            lower_adjustment_state.layers.push_back(matrix_adjustment);
            const auto lower_adjustment = render(lower_adjustment_state);
            auto upper_adjustment_state = foreground_only_state;
            upper_adjustment_state.layers.prepend(matrix_adjustment);
            const auto upper_adjustment = render(upper_adjustment_state);
            CHECK(!foreground_only.isNull() && lower_adjustment.size() == foreground_only.size() &&
                upper_adjustment.size() == foreground_only.size(),
                "Ordering fixture renders both adjustment positions");
            if (!foreground_visible) {
              auto adjustment_only_state = EditRecipeState{};
              adjustment_only_state.layers = {matrix_adjustment};
              CHECK(lower_adjustment == render(adjustment_only_state),
                  "A hidden foreground does not intercept or change the lower adjustment target");
            }
            bool saw_opaque_foreground = false;
            bool saw_lower_contribution = false;
            for (int y = 0; y < foreground_only.height(); ++y) {
              for (int x = 0; x < foreground_only.width(); ++x) {
                const auto foreground_pixel = foreground_only.pixelColor(x, y);
                const auto foreground_alpha_value = foreground_alpha.pixelColor(x, y).alpha();
                if (foreground_visible && foreground_alpha_value == 255) {
                  if (foreground_opacity == 1.0) {
                    saw_opaque_foreground = true;
                    CHECK(lower_adjustment.pixelColor(x, y) == foreground_pixel,
                        "An adjustment below opaque foreground cannot alter its pixels");
                  }
                } else if (foreground_visible && foreground_opacity < 1.0 && foreground_alpha_value > 0 &&
                    lower_adjustment.pixelColor(x, y) != foreground_pixel) {
                  saw_lower_contribution = true;
                }
              }
            }
            CHECK(saw_opaque_foreground || foreground_opacity < 1.0 || !foreground_visible,
                "Ordering fixture includes visible opaque foreground pixels");
            if (foreground_visible && foreground_opacity < 1.0)
              CHECK(saw_lower_contribution,
                  "A translucent foreground correctly reveals its adjusted lower stack");
            CHECK(upper_adjustment != foreground_only,
                "An adjustment above the foreground changes the composed result");
            ++ordering_fixtures;
          }
        }
      }
    }
  }
  CHECK(ordering_fixtures == 48,
      "The top-first matrix must cover 48 fixtures, including the plan's 24-case shared gate");

  QByteArray alpha(source.width() * source.height(), char(0));
  for (int y = 0; y < source.height(); ++y)
    for (int x = 210; x < 270; ++x) alpha[y * source.width() + x] = char(255);
  state = {};
  state.mask_stack = {QVariantMap{{"id", "subject-component"}, {"maskSetId", "subject"},
      {"maskSetName", "Subject"}, {"toolRole", "selection"}, {"kind", "brush"},
      {"enabled", true}, {"operation", "add"}, {"segmentationWidth", source.width()},
      {"segmentationHeight", source.height()}, {"segmentationData", alpha}}};
  const auto path = temp.filePath("photo.png");
  CHECK(source.save(path), "Save fixture");
  EditorPreviewImageProvider provider;
  auto controller = std::make_unique<EditorPreviewController>();
  controller->setImageProvider(&provider);
  controller->loadAsset("composition", path, {}, {}, {}, {}, serialize_edit_recipe_json(state));
  QElapsedTimer timer; timer.start();
  while (controller->displayPreviewSource().isEmpty() && timer.elapsed() < 20000) {
    QCoreApplication::processEvents(); QThread::msleep(5);
  }
  CHECK(!controller->displayPreviewSource().isEmpty(), "Loaded preview");
  if (app.arguments().contains(QStringLiteral("--multi-document-agent-contract")))
    return run_agent_multi_document_contract(*controller, temp, source);
  if (app.arguments().contains(QStringLiteral("--native-command-registry-contract"))) {
    const auto baseline = controller->currentAssetBoundAdjustmentPatchJson();
    AgentAppCommandRegistry registry;
    QString active_task;
    bool style_allowed = true;
    AgentEditController agent;
    agent.setEditingFreedom("transform");
    register_agent_photo_commands(registry, controller.get(), [&]() { return active_task; },
        [&](const QString& name, bool property) -> QVariantMap {
          return style_allowed ? agent.authorizeNativeEditingCommand(name, property)
              : QVariantMap{{"ok", false}, {"error", "Style disallows this control."}};
        });
    CatalogAssetModel catalog;
    PhotoAutomationBridge bridge(&agent, &catalog);
    bridge.setAppCommandRegistry(&registry);
    bool license_allowed = true;
    bridge.setEditAccessPolicy([&]() { return license_allowed; });
    AgentMcpClient client;
    client.setEndpoint(bridge.agentMcpEndpoint());
    client.setAllowedTools({"app.list_commands", "app.command_schema", "app.inspect_command", "app.invoke_command", "photo.edit_composition"});
    CHECK(client.refreshToolCache("native-command-registry").value("ok").toBool() &&
        client.hasCachedTool("app.invoke_command"), "Production MCP discovery exposes native commands");
    bool layer_requirement_discovered = false;
    for (const auto& value : client.cachedTools()) {
      const auto tool = value.toMap();
      if (tool.value("name") != "photo.edit_composition") continue;
      for (const auto& condition : tool.value("inputSchema").toMap().value("allOf").toList()) {
        const auto branch = condition.toMap();
        if (!branch.value("then").toMap().value("required").toStringList().contains("layerId")) continue;
        const auto operations = branch.value("if").toMap().value("properties").toMap()
            .value("operation").toMap().value("enum").toStringList();
        layer_requirement_discovered = branch.value("if").toMap().value("additionalProperties").toBool() && operations.contains("typography") && operations.contains("paint") &&
            operations.contains("erase_selection") && !operations.contains("add_text") && !operations.contains("copy_selection");
      }
    }
    CHECK(layer_requirement_discovered, "Actual discovered schema distinguishes existing-layer edits from creation/base copying");
    int composition_dispatches = 0;
    bridge.setCompositionHandler([&](const QVariantMap& command) {
      ++composition_dispatches;
      return controller->photoCompositionCommand(command);
    });
    for (const auto& id : {QVariant{}, QVariant(QString{}), QVariant(QStringLiteral("   "))}) {
      QVariantMap arguments{{"assetId", "composition"}, {"operation", "typography"},
          {"values", QVariantMap{{"family", "Georgia"}, {"size", 80}}}};
      if (id.isValid()) arguments.insert("layerId", id);
      const auto rejected = client.callTool("photo.edit_composition", arguments);
      const auto rejection = rejected.value("structuredContent").toMap();
      CHECK(!rejected.value("ok").toBool() && rejection.value("recoverableError").toBool() &&
          rejection.value("mutationNotApplied").toBool() &&
          composition_dispatches == 0 && controller->currentAssetBoundAdjustmentPatchJson() == baseline,
          "Missing or empty layer targets are correctable unapplied errors before any document dispatch");
    }
    for (const auto& values : {QVariantMap{{"family", "Georgia"}, {"x", 80}},
        QVariantMap{{"weight", "bold"}}, QVariantMap{{"size", -1}}, QVariantMap{}}) {
      const auto rejected = client.callTool("photo.edit_composition", {{"assetId", "composition"},
          {"operation", "typography"}, {"layerId", "schema-target"}, {"values", values}});
      const auto rejection = rejected.value("structuredContent").toMap();
      CHECK(!rejected.value("ok").toBool() && rejection.value("recoverableError").toBool() &&
          rejection.value("mutationNotApplied").toBool() && composition_dispatches == 0 &&
          controller->currentAssetBoundAdjustmentPatchJson() == baseline,
          "Typographic coordinates, invalid types/ranges and empty updates fail before document dispatch");
    }
    const auto content = [](const QVariantMap& result) { return result.value("structuredContent").toMap(); };
    const auto discover = [&](const QString& query) {
      const auto result = client.callTool("app.list_commands", {{"domain", "edit"}, {"query", query}});
      const auto rows = content(result).value("commands").toList();
      for (const auto& item : rows) {
        const auto row = item.toMap();
        if (row.value("name") == query && row.value("available").toBool()) return row.value("commandId").toString();
      }
      return QString{};
    };
    const auto fill_command = discover("addFillLayer");
    const auto shape_command = discover("addPhotoShape");
    const auto source_command = discover("addSourceLayer");
    const auto group_command = discover("groupPhotoLayers");
    const auto opacity_command = discover("setPhotoLayerOpacity");
    const auto exposure_command = discover("exposure");
    CHECK(!fill_command.isEmpty() && !shape_command.isEmpty() && !source_command.isEmpty() &&
        !group_command.isEmpty() && !opacity_command.isEmpty() && !exposure_command.isEmpty(), "Previously missing design and color controls are available");
    CHECK(!client.callTool("app.inspect_command", {{"commandId", fill_command}, {"values", QVariantMap{}}}).value("ok").toBool() &&
        controller->currentAssetBoundAdjustmentPatchJson() == baseline, "Read-only tool cannot dispatch any mutation");
    CHECK(client.callTool("app.inspect_command", {{"commandId", discover("availablePhotoTextFonts")},
        {"values", QVariantMap{}}}).value("ok").toBool(), "Read-only native inspection is reachable without task ownership");
    const auto invoke = [&](const QString& id, const QVariantMap& values) {
      return client.callTool("app.invoke_command", {{"commandId", id}, {"values", values}});
    };
    CHECK(!invoke(fill_command, {{"color", "#172130"}, {"name", "Background"}}).value("ok").toBool() &&
        controller->currentAssetBoundAdjustmentPatchJson() == baseline, "MCP refuses mutation without task ownership");
    active_task = "native-registry-task";
    CHECK(controller->beginAgentTask(active_task).value("ok").toBool(), "Capture exact native design baseline");
    style_allowed = false;
    CHECK(!invoke(fill_command, {{"color", "#172130"}}).value("ok").toBool() &&
        controller->currentAssetBoundAdjustmentPatchJson() == baseline, "Style admission occurs before mutation");
    style_allowed = true;
    agent.setEditingFreedom("protect");
    CHECK(!invoke(fill_command, {{"color", "#172130"}}).value("ok").toBool() &&
        controller->currentAssetBoundAdjustmentPatchJson() == baseline, "Protect mode preserves layer and graphic permissions on native tools");
    agent.setEditingFreedom("transform");
    license_allowed = false;
    CHECK(!invoke(fill_command, {{"color", "#172130"}}).value("ok").toBool() &&
        controller->currentAssetBoundAdjustmentPatchJson() == baseline, "Existing license policy still guards native commands");
    license_allowed = true;
    const auto fill = invoke(fill_command, {{"color", "#172130"}, {"name", "Background"}});
    const auto fill_id = content(fill).value("createdLayerId").toString();
    CHECK(fill.value("ok").toBool() && !fill_id.isEmpty() && content(fill).value("changed").toBool(), "Native fill returns its actual created ID and checkpoint");
    const auto shape_schema = content(client.callTool("app.command_schema", {{"commandId", shape_command}}));
    CHECK(shape_schema.value("inputSchema").toMap().value("required").toList().size() == 10, "Shape tool uses all ten actual typed native arguments");
    const auto shape = invoke(shape_command, {{"type", "rounded-rectangle"}, {"start_x", 24}, {"start_y", 20},
        {"end_x", 456}, {"end_y", 90}, {"canvas_width", 480}, {"canvas_height", 320},
        {"fill_color", "#ffc945"}, {"stroke_color", "#fff1ca"}, {"stroke_width", 2.0}});
    const auto shape_id = content(shape).value("createdLayerId").toString();
    CHECK(shape.value("ok").toBool() && !shape_id.isEmpty(), "Native shape renders and remains an editable layer");
    QImage reference(80, 60, QImage::Format_RGBA8888);
    reference.fill(QColor("#538aaa"));
    const auto reference_path = temp.filePath("reference.png");
    CHECK(reference.save(reference_path), "Save independent source fixture");
    const auto placed = invoke(source_command, {{"file_path", reference_path}, {"name", "Reference inset"}});
    const auto source_id = content(placed).value("createdLayerId").toString();
    CHECK(placed.value("ok").toBool() && !source_id.isEmpty(), "Place an actual source image through MCP");
    CHECK(invoke(opacity_command, {{"layerId", fill_id}, {"opacity", 0.85}}).value("ok").toBool(), "Stable ID follows fill after newer layers move its row");
    const auto no_op = invoke(opacity_command, {{"layerId", fill_id}, {"opacity", 0.85}});
    CHECK(no_op.value("ok").toBool() && content(no_op).value("alreadySatisfied").toBool() &&
        !content(no_op).value("changed").toBool(), "Verified idempotent native setter completes without a false failure");
    const auto group = invoke(group_command, {{"layerIds", QVariantList{shape_id, source_id}}, {"name", "Offer content"}});
    CHECK(group.value("ok").toBool() && !content(group).value("createdLayerId").toString().isEmpty(), "Group real layers using IDs resolved at dispatch");
    CHECK(invoke(exposure_command, {{"value", 0.3}}).value("ok").toBool(), "Color property is tracked by the same design task");
    const auto title = invoke(discover("addPointTextLayer"), {{"text", "NEW LOOK"}, {"x", 40}, {"y", 28},
        {"canvas_width", 480}, {"canvas_height", 320}});
    const auto title_id = content(title).value("createdLayerId").toString();
    CHECK(title.value("ok").toBool() && !title_id.isEmpty(), "Native typography creation returns a real editable title ID");
    CHECK(invoke(discover("setPhotoLayerTextStyle"), {{"layerId", title_id}, {"font_family", QGuiApplication::font().family()},
        {"font_size", 38.0}, {"color", "#172130"}, {"font_weight", "700"}, {"alignment", "left"}}).value("ok").toBool(),
        "Style the actual editable title through native typography controls");
    const auto transform_command = discover("setPhotoLayerTransform");
    const QVariantMap transform_values{{"layerId", title_id}, {"x", 45}, {"y", 32}, {"scale", 1.1}, {"rotation", 3.0}};
    CHECK(invoke(transform_command, transform_values).value("ok").toBool(), "Apply actual native title transform");
    const auto unchanged_transform = invoke(transform_command, transform_values);
    CHECK(unchanged_transform.value("ok").toBool() && content(unchanged_transform).value("alreadySatisfied").toBool() &&
        !content(unchanged_transform).value("changed").toBool(), "Repeated native transform is verified success");
    const auto perspective_command = discover("setPhotoLayerPerspective");
    const QVariantMap perspective_values{{"layerId", title_id}, {"horizontal", 5.0}, {"vertical", 2.0}};
    CHECK(invoke(perspective_command, perspective_values).value("ok").toBool(), "Apply actual native text perspective");
    CHECK(content(invoke(perspective_command, perspective_values)).value("alreadySatisfied").toBool(),
        "Repeated native perspective is verified success");
    const auto before_invalid = controller->currentAssetBoundAdjustmentPatchJson();
    const auto invalid_transform = invoke(transform_command, {{"layerId", "missing"}, {"x", 45}, {"y", 32}, {"scale", 1.1}, {"rotation", 3.0}});
    CHECK(!invalid_transform.value("ok").toBool() && content(invalid_transform).value("recoverableError").toBool() &&
        content(invalid_transform).value("mutationNotApplied").toBool() && controller->currentAssetBoundAdjustmentPatchJson() == before_invalid,
        "Production MCP preserves safe argument rejection without mutation");
    const auto invalid_style = invoke(discover("setPhotoLayerTypography"), {{"layerId", title_id},
        {"changes", QVariantMap{{"face", "not-an-installed-face"}}}});
    CHECK(!invalid_style.value("ok").toBool() && content(invalid_style).value("recoverableError").toBool() &&
        content(invalid_style).value("mutationNotApplied").toBool() && !content(invalid_style).value("changed").toBool(),
        "Native owner certifies unchanged synchronous rejection for planner correction");
    for (int index = 0; index < 12; ++index)
      CHECK(invoke(opacity_command, {{"layerId", fill_id}, {"opacity", index % 2 == 0 ? 0.82 : 0.88}}).value("ok").toBool(),
          "Actual native design keeps checkpointing beyond the former eight-operation cap");
    CHECK(controller->agentTaskState().value("steps").toList().size() >= 21, "Journal every completed native design operation");
    const auto modified = controller->currentAssetBoundAdjustmentPatchJson();
    CHECK(!invoke(opacity_command, {{"layerId", "deleted-layer"}, {"opacity", 0.2}}).value("ok").toBool() &&
        controller->currentAssetBoundAdjustmentPatchJson() == modified, "Missing IDs cannot target another layer by accident");
    const auto export_rows = content(client.callTool("app.list_commands", {{"domain", "edit"}, {"query", "exportFlattenedPhoto"}})).value("commands").toList();
    CHECK(!export_rows.isEmpty() && !export_rows.first().toMap().value("available").toBool(), "Const file-writing methods do not bypass output ownership");
    const auto modified_state = parse_adjustment_patch_json(modified);
    const auto baseline_state = parse_adjustment_patch_json(baseline);
    CHECK(modified_state.ok() && baseline_state.ok(), "Parse native asset-bound recipes through their actual format");
    const auto rendered = render(*modified_state.state);
    CHECK(!rendered.isNull() && rendered != source, "Native design commands produce changed rendered pixels");
    CHECK(controller->finishAgentTask(active_task, "review").value("ok").toBool(), "Keep design recipe reversible at review");
    CHECK(controller->revertAgentTask(active_task).value("ok").toBool() &&
        controller->currentAssetBoundAdjustmentPatchJson() == baseline, "Revert every native design and color change together");
    const auto reverted_state = parse_adjustment_patch_json(controller->currentAssetBoundAdjustmentPatchJson());
    CHECK(reverted_state.ok() && render(*reverted_state.state) == render(*baseline_state.state),
        "Reversion restores exact rendered baseline pixels");
    active_task.clear();
    std::cout << "Production MCP native design discovery, shape/fill/source/group/color, guards, stable IDs and exact reversal passed.\n";
    return 0;
  }
  if (app.arguments().contains(QStringLiteral("--composition-task-transaction-contract"))) {
    const auto baseline = controller->currentAssetBoundAdjustmentPatchJson();
    const auto task_command = [&](const QString& task, const QString& op, const QVariantMap& values = {}, const QString& layer = {}) {
      return controller->runAgentTaskComposition(task, {{"assetId", "composition"}, {"operation", op},
          {"values", values}, {"layerId", layer}});
    };
    CHECK(controller->beginAgentTask("native-long-task").value("ok").toBool(), "Capture the native recipe before any tool changes it");
    const auto invalid_title = task_command("native-long-task", "add_text",
        {{"text", "LOOKSY TEST"}, {"x", 0.68}, {"y", 0.15}});
    CHECK(!invalid_title.value("ok").toBool() &&
        invalid_title.value("recoverableError").toBool() &&
        invalid_title.value("mutationNotApplied").toBool() &&
        !invalid_title.value("changed").toBool() &&
        invalid_title.value("errorCode").toString() == "invalid_composition_coordinate" &&
        invalid_title.value("error").toString().contains("pixels") &&
        controller->currentAssetBoundAdjustmentPatchJson() == baseline,
        "Reject normalized text coordinates before mutation and allow the owned task to correct them");
    const auto title = task_command("native-long-task", "add_text", {{"text", "LOOKSY TEST"}, {"x", 8}, {"y", 12}});
    const auto title_id = title.value("createdLayerId").toString();
    CHECK(title.value("ok").toBool() && !title_id.isEmpty(), "Create an actual editable native title through the task owner");
    for (int i = 0; i < 23; ++i)
      CHECK(task_command("native-long-task", "opacity", {{"opacity", i % 2 == 0 ? 0.8 : 0.9}}, title_id).value("ok").toBool(),
          "Native task can checkpoint more than eight changes without a whole-job deadline");
    controller->setExposure(0.35);
    const auto authored = controller->currentAssetBoundAdjustmentPatchJson();
    CHECK(controller->finishAgentTask("native-long-task", "review", authored).value("ok").toBool(), "Track the exact authored final color recipe");
    CHECK(controller->agentTaskState().value("steps").toList().size() >= 24, "Preserve every completed native step");
    CHECK(controller->revertAgentTask("native-long-task").value("ok").toBool(), "Revert native composition plus final color changes");
    CHECK(controller->currentAssetBoundAdjustmentPatchJson() == baseline, "Whole-task revert restores exact baseline recipe bytes");

    CHECK(controller->beginAgentTask("native-cancel-task").value("ok").toBool(), "Start owned pixel processing");
    const auto pixel = task_command("native-cancel-task", "add_pixel_layer");
    const auto pixel_id = pixel.value("createdLayerId").toString();
    CHECK(pixel.value("ok").toBool() && !pixel_id.isEmpty(), "Create owned paint surface");
    const QVariantList points{QVariantMap{{"x", 15}, {"y", 15}, {"pressure", 1.0}},
        QVariantMap{{"x", 35}, {"y", 35}, {"pressure", 1.0}}};
    const auto paint = task_command("native-cancel-task", "paint", {{"points", points}, {"color", "#ffff0000"},
        {"size", 15.0}}, pixel_id);
    CHECK(paint.value("ok").toBool() && controller->photoPaintBusy(), "Real native pixels remain pending after acceptance");
    CHECK(controller->revertAgentTask("native-cancel-task").value("ok").toBool(), "Cancel owned paint and restore the recipe");
    QElapsedTimer drain;
    drain.start();
    while (drain.elapsed() < 300) { QCoreApplication::processEvents(); QThread::msleep(5); }
    CHECK(!controller->photoPaintBusy() && controller->currentAssetBoundAdjustmentPatchJson() == baseline,
        "A late native paint completion cannot overwrite the reverted task");

    CHECK(controller->beginAgentTask("native-conflict-task").value("ok").toBool(), "Start manual-change preservation fixture");
    const auto changed = task_command("native-conflict-task", "add_text", {{"text", "TASK TITLE"}, {"x", 8}, {"y", 12}});
    const auto changed_id = changed.value("createdLayerId").toString();
    CHECK(changed.value("ok").toBool(), "Create conflict fixture text");
    CHECK(controller->finishAgentTask("native-conflict-task", "review").value("ok").toBool(), "Keep completed native task reversible");
    CHECK(controller->setPhotoLayerOpacity(0, 0.4), "Make a subsequent manual edit");
    const auto manual_state = controller->currentAssetBoundAdjustmentPatchJson();
    CHECK(!controller->revertAgentTask("native-conflict-task").value("ok").toBool() &&
        controller->currentAssetBoundAdjustmentPatchJson() == manual_state, "Revert preserves subsequent manual edits and the journal");
    CHECK(controller->setPhotoLayerOpacity(0, 1.0), "Return conflict fixture to its owned checkpoint");
    const auto saved = controller->currentAssetBoundAdjustmentPatchJson();
    controller.reset();
    EditorPreviewImageProvider recovered_provider;
    controller = std::make_unique<EditorPreviewController>();
    controller->setImageProvider(&recovered_provider);
    controller->loadAsset("composition", path, {}, {}, {}, {}, saved);
    CHECK(controller->recoverAgentTask("native-conflict-task").value("ok").toBool(), "Recover native task after controller recreation");
    CHECK(controller->revertAgentTask("native-conflict-task").value("ok").toBool() &&
        controller->currentAssetBoundAdjustmentPatchJson() == baseline, "Recovered native task restores its exact original recipe");
    std::cout << "Native long task, complete reversal, cancel without late writes, conflict preservation and recovery passed.\n";
    return 0;
  }
  const auto command = [&](const QString& op, const QVariantMap& values = {}, const QString& id = {}) {
    return controller->photoCompositionCommand(QJsonObject::fromVariantMap({{"assetId", "composition"}, {"operation", op},
        {"values", values}, {"layerId", id}}).toVariantMap());
  };
  ControllerMcpEndpoint mcp_endpoint(controller.get());
  const auto internal_tools = qEnvironmentVariableIsSet("PHOTO_EDITOR_INTERNAL_COMPOSITION_PROOF");
  if (!internal_tools && !mcp_endpoint.start()) {
    std::cerr << "composition MCP loopback listen error: "
              << mcp_endpoint.errorString().toStdString() << '\n';
    return fail("Start composition MCP loopback endpoint");
  }
  CatalogAssetModel internal_catalog;
  AgentEditController internal_agent;
  PhotoAutomationBridge internal_bridge(&internal_agent, &internal_catalog);
  internal_bridge.setCompositionHandler([&](const QVariantMap& request) {
    return controller->photoCompositionCommand(request);
  });
  AgentMcpClient mcp_client;
  mcp_client.setEndpoint(internal_tools ? internal_bridge.agentMcpEndpoint() : mcp_endpoint.endpoint());
  mcp_client.setBearerToken(QStringLiteral("composition-test-capability"));
  mcp_client.setTimeoutMs(3000);
  mcp_client.setAllowedTools({QStringLiteral("photo.inspect_composition"),
      QStringLiteral("photo.edit_composition")});
  const auto mcp_tools = mcp_client.refreshToolCache(QStringLiteral("composition-controller-test"));
  CHECK(mcp_tools.value(QStringLiteral("ok")).toBool() &&
      mcp_client.hasCachedTool(QStringLiteral("photo.inspect_composition")) &&
      mcp_client.hasCachedTool(QStringLiteral("photo.edit_composition")),
      "Agent MCP client discovers the controller-backed composition tool");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_RUN_LIVE_COMPOSITION_AGENT")) {
    const auto live_app_data = qEnvironmentVariable("PHOTO_EDITOR_LIVE_COMPOSITION_APP_DATA_DIR").trimmed();
    const auto live_model_id = qEnvironmentVariable(
        "PHOTO_EDITOR_LIVE_COMPOSITION_MODEL_ID", QStringLiteral("ollama-qwen3-vl-32b")).trimmed();
    const auto receipt_dir = qEnvironmentVariable("PHOTO_EDITOR_LIVE_COMPOSITION_RECEIPT_DIR").trimmed();
    if (live_app_data.isEmpty() || receipt_dir.isEmpty())
      return fail("Live composition-agent workflow requires app-data and receipt directories");
    qputenv("PHOTO_EDITOR_APP_DATA_DIR", live_app_data.toUtf8());
    const auto model_probe = probe_agent_local_model(live_model_id);
    if (!model_probe.value(QStringLiteral("ok")).toBool() ||
        !model_probe.value(QStringLiteral("available")).toBool()) {
      std::cerr << "live composition model probe: "
                << QJsonDocument::fromVariant(model_probe).toJson(QJsonDocument::Compact).toStdString()
                << '\n';
      return fail("Configured local model must be reachable for the live composition-agent run");
    }
    QVariantMap style;
    for (const auto& value : built_in_agent_edit_styles()) {
      const auto candidate = value.toMap();
      if (candidate.value(QStringLiteral("id")).toString() == QStringLiteral("warm-editorial-wedding")) {
        style = candidate;
        break;
      }
    }
    if (style.isEmpty()) return fail("The live composition-agent run requires the Warm Editorial style");
    const auto mask_set_id = real_photo_review ? QStringLiteral("sam2-subject-selection")
                                               : QStringLiteral("subject");
    const QString creative_brief = QStringLiteral(
        "Create an editorial image composition with the exact headline FLOAT visibly behind the selected "
        "foreground subject. The active assetId is composition. The existing selection has maskSetId %1. "
        "Do not inspect; both identifiers are explicit. Return exactly two photo.edit_composition tool calls "
        "in order. First add a bold, readable headline "
        "with photo.edit_composition operation add_text at x=120 and y=115, then duplicate the selected "
        "base-image pixels above it with operation copy_selection, maskId %1, and independent=false. "
        "Preserve the image and keep the edit reversible. Do not use any other tool.").arg(mask_set_id);
    AgentLocalModelPlanRequest live_request;
    live_request.model_id = live_model_id;
    live_request.style = style;
    live_request.asset = {{QStringLiteral("assetId"), QStringLiteral("composition")},
        {QStringLiteral("filePath"), path}};
    live_request.current_adjustment_patch_json = QStringLiteral(
        "{\"kind\":\"photo-editor-adjustment-patch\",\"version\":1,"
        "\"scope\":\"asset-bound\",\"groups\":{}}");
    live_request.user_instructions = creative_brief;
    live_request.available_tool_schemas = mcp_tools.value(QStringLiteral("tools")).toList();
    live_request.allow_tool_call_plan = true;
    live_request.force_model_planning = true;
    live_request.allow_deterministic_fallback = false;
    live_request.strategy_phase = QStringLiteral("sample");
    QElapsedTimer model_timer;
    model_timer.start();
    const auto model_plan = plan_agent_recipe_patch(live_request);
    const auto model_elapsed_ms = model_timer.elapsed();
    if (!model_plan.ok || model_plan.decision != QStringLiteral("tool_call_plan") ||
        model_plan.tool_call_plan.isEmpty() ||
        model_plan.prompt_audit.value(QStringLiteral("backend")).toString() != QStringLiteral("ollama") ||
        model_plan.prompt_audit.value(QStringLiteral("fallbackUsed")).toBool()) {
      std::cerr << "live composition tool planning failed: " << model_plan.error_message.toStdString()
                << " audit=" << QJsonDocument::fromVariant(model_plan.prompt_audit)
                       .toJson(QJsonDocument::Compact).toStdString() << '\n';
      return fail("Local vision model must produce a composition MCP tool-call plan without fallback");
    }
    if (!QDir().mkpath(receipt_dir)) return fail("Create live composition-agent receipt directory");
    const auto tool_plan_path = QDir(receipt_dir).filePath(QStringLiteral("tool-plan.json"));
    QFile tool_plan_file(tool_plan_path);
    const QVariantMap tool_plan_receipt{
        {QStringLiteral("contract"), QStringLiteral("agent-live-composition-tool-plan-attempt-v1")},
        {QStringLiteral("modelId"), live_model_id},
        {QStringLiteral("creativeBrief"), creative_brief},
        {QStringLiteral("modelPlan"), QVariantMap{
            {QStringLiteral("decision"), model_plan.decision},
            {QStringLiteral("summary"), model_plan.plan_summary},
            {QStringLiteral("toolCalls"), model_plan.tool_call_plan},
            {QStringLiteral("promptAudit"), model_plan.prompt_audit},
            {QStringLiteral("elapsedMs"), model_elapsed_ms}}},
        {QStringLiteral("dispatchStatus"), QStringLiteral("pending")}};
    if (!tool_plan_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        tool_plan_file.write(QJsonDocument::fromVariant(tool_plan_receipt).toJson(QJsonDocument::Indented)) <= 0)
      return fail("Persist the model-proposed composition tool plan before dispatch");
    tool_plan_file.close();
    QVariantList tool_reports;
    EditRecipeState text_only_state;
    auto text_only_captured = false;
    auto saw_add_text = false;
    auto saw_subject_copy = false;
    for (const auto& call_value : model_plan.tool_call_plan) {
      const auto call = call_value.toMap();
      const auto name = call.value(QStringLiteral("name")).toString();
      const auto arguments = call.value(QStringLiteral("arguments")).toMap();
      const auto operation = arguments.value(QStringLiteral("operation")).toString();
      const auto values = arguments.value(QStringLiteral("values")).toMap();
      saw_add_text = saw_add_text || (name == QStringLiteral("photo.edit_composition") &&
          operation == QStringLiteral("add_text") && values.value(QStringLiteral("text")).toString() == QStringLiteral("FLOAT"));
      saw_subject_copy = saw_subject_copy || (name == QStringLiteral("photo.edit_composition") &&
          operation == QStringLiteral("copy_selection") &&
          values.value(QStringLiteral("maskId")).toString() == mask_set_id);
      const auto report = mcp_client.callTool(name, arguments);
      if (!report.value(QStringLiteral("ok")).toBool()) {
        auto failed_plan_receipt = tool_plan_receipt;
        failed_plan_receipt.insert(QStringLiteral("dispatchStatus"), QStringLiteral("failed"));
        failed_plan_receipt.insert(QStringLiteral("failedToolCall"), call);
        failed_plan_receipt.insert(QStringLiteral("failedToolReport"), report);
        QFile failed_file(tool_plan_path);
        if (failed_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
          failed_file.write(QJsonDocument::fromVariant(failed_plan_receipt).toJson(QJsonDocument::Indented));
        std::cerr << "live composition MCP tool failed: "
                  << QJsonDocument::fromVariant(report).toJson(QJsonDocument::Compact).toStdString()
                  << '\n';
        return fail("Every model-requested composition operation must execute successfully");
      }
      const auto structured_result = report.value(QStringLiteral("structuredContent")).toMap();
      QVariantList layer_summaries;
      for (const auto& layer_value : structured_result.value(QStringLiteral("layers")).toList()) {
        const auto layer = layer_value.toMap();
        QVariantMap summary;
        for (const auto* key : {"id", "kind", "name", "position", "text", "fontFamily",
                                 "fontSize", "textColor", "selectionScoped", "copyBaseImage"}) {
          if (layer.contains(QLatin1String(key))) summary.insert(QLatin1String(key), layer.value(QLatin1String(key)));
        }
        layer_summaries.append(summary);
      }
      QVariantMap compact_report{
          {QStringLiteral("toolName"), name},
          {QStringLiteral("arguments"), arguments},
          {QStringLiteral("ok"), report.value(QStringLiteral("ok"))},
          {QStringLiteral("operation"), operation},
          {QStringLiteral("assetId"), arguments.value(QStringLiteral("assetId"))}};
      for (const auto* key : {"changed", "order", "width", "height", "error"}) {
        if (structured_result.contains(QLatin1String(key)))
          compact_report.insert(QLatin1String(key), structured_result.value(QLatin1String(key)));
      }
      if (!layer_summaries.isEmpty()) compact_report.insert(QStringLiteral("layers"), layer_summaries);
      if (structured_result.contains(QStringLiteral("masks")))
        compact_report.insert(QStringLiteral("masks"), structured_result.value(QStringLiteral("masks")));
      tool_reports.append(compact_report);
      if (operation == QStringLiteral("add_text")) {
        text_only_state = EditorPreviewControllerTestAccess::state(*controller);
        text_only_captured = true;
      }
    }
    if (!saw_add_text || !saw_subject_copy) {
      auto incomplete_plan_receipt = tool_plan_receipt;
      incomplete_plan_receipt.insert(QStringLiteral("dispatchStatus"), QStringLiteral("incomplete-plan"));
      incomplete_plan_receipt.insert(QStringLiteral("toolReports"), tool_reports);
      QFile incomplete_file(tool_plan_path);
      if (incomplete_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        incomplete_file.write(QJsonDocument::fromVariant(incomplete_plan_receipt).toJson(QJsonDocument::Indented));
      return fail("Live composition tool plan must add FLOAT text and copy the existing subject selection");
    }
    const auto final_state = EditorPreviewControllerTestAccess::state(*controller);
    const auto final_recipe = serialize_edit_recipe_json(final_state);
    const auto reopened = parse_edit_recipe_json(final_recipe);
    const auto final_image = render(final_state);
    auto base_state = final_state;
    base_state.layers.clear();
    const auto base_image = render(base_state);
    const auto text_only_image = text_only_captured ? render(text_only_state) : QImage{};
    auto occluded_glyph_pixels = 0;
    if (!base_image.isNull() && !text_only_image.isNull() && !final_image.isNull()) {
      for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
          if (text_only_image.pixel(x, y) != base_image.pixel(x, y) &&
              final_image.pixel(x, y) == base_image.pixel(x, y))
            ++occluded_glyph_pixels;
        }
      }
    }
    const auto final_layers = final_state.layers;
    const auto cutout_layer = final_layers.isEmpty() ? QVariantMap{} : final_layers.first().toMap();
    const auto text_layer = final_layers.size() < 2 ? QVariantMap{} : final_layers.at(1).toMap();
    if (!validate_edit_recipe_json(final_recipe).valid || final_image.isNull() || render(reopened) != final_image ||
        !text_only_captured || final_layers.size() < 2 ||
        !cutout_layer.value(QStringLiteral("selectionScoped")).toBool() ||
        text_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("text") ||
        text_layer.value(QStringLiteral("text")).toString() != QStringLiteral("FLOAT") ||
        occluded_glyph_pixels <= 0)
      return fail("Live composition tool result must preserve an exact valid recipe reopen");
    const auto recipe_path = QDir(receipt_dir).filePath(QStringLiteral("final-recipe.json"));
    const auto image_path = QDir(receipt_dir).filePath(QStringLiteral("final-render.png"));
    const auto source_fixture_path = QDir(receipt_dir).filePath(QStringLiteral("source-fixture.png"));
    const auto selection_mask_path = QDir(receipt_dir).filePath(QStringLiteral("selection-mask.png"));
    const auto mask_bytes = final_state.mask_stack.isEmpty() ? QByteArray{} :
        final_state.mask_stack.first().toMap().value(QStringLiteral("segmentationData")).toByteArray();
    QImage selection_mask(source.size(), QImage::Format_Grayscale8);
    if (mask_bytes.size() == source.width() * source.height()) {
      for (int y = 0; y < source.height(); ++y) {
        auto* row = selection_mask.scanLine(y);
        for (int x = 0; x < source.width(); ++x)
          row[x] = static_cast<unsigned char>(mask_bytes.at(y * source.width() + x));
      }
    }
    QFile recipe_file(recipe_path);
    if (!recipe_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        recipe_file.write(final_recipe.toUtf8()) != final_recipe.toUtf8().size() ||
        !final_image.save(image_path) || !source.save(source_fixture_path) ||
        selection_mask.isNull() || !selection_mask.save(selection_mask_path))
      return fail("Persist live model composition recipe and rendered proof");
    recipe_file.close();
    const auto file_sha256 = [](const QString& file_path) {
      if (file_path.trimmed().isEmpty()) return QString{};
      QFile file(file_path);
      if (!file.open(QIODevice::ReadOnly)) return QString{};
      return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
    };
    const QVariantMap receipt{
        {QStringLiteral("contract"), QStringLiteral("agent-live-composition-tool-run-v1")},
        {QStringLiteral("modelId"), live_model_id},
        {QStringLiteral("modelFamily"), model_probe.value(QStringLiteral("modelFamily"))},
        {QStringLiteral("ollamaModel"), model_probe.value(QStringLiteral("ollamaModel"))},
        {QStringLiteral("creativeBrief"), creative_brief},
        {QStringLiteral("localOnly"), true}, {QStringLiteral("cloudUsed"), false},
        {QStringLiteral("automaticCommitAllowed"), false},
        {QStringLiteral("modelPlan"), QVariantMap{
            {QStringLiteral("decision"), model_plan.decision},
            {QStringLiteral("summary"), model_plan.plan_summary},
            {QStringLiteral("toolCalls"), model_plan.tool_call_plan},
            {QStringLiteral("promptAudit"), model_plan.prompt_audit},
            {QStringLiteral("elapsedMs"), model_elapsed_ms}}},
        {QStringLiteral("toolReports"), tool_reports},
        {QStringLiteral("finalRecipePath"), recipe_path},
        {QStringLiteral("finalRecipeSha256"), file_sha256(recipe_path)},
        {QStringLiteral("finalRenderPath"), image_path},
        {QStringLiteral("finalRenderSha256"), file_sha256(image_path)},
        {QStringLiteral("sourceImageSha256"), file_sha256(path)},
        {QStringLiteral("subjectMaskSha256"), file_sha256(review_mask_path)},
        {QStringLiteral("selectionMaskAlphaSha256"), QString::fromLatin1(
            QCryptographicHash::hash(mask_bytes, QCryptographicHash::Sha256).toHex())},
        {QStringLiteral("sourceFixturePath"), source_fixture_path},
        {QStringLiteral("sourceFixtureSha256"), file_sha256(source_fixture_path)},
        {QStringLiteral("selectionMaskPath"), selection_mask_path},
        {QStringLiteral("selectionMaskFileSha256"), file_sha256(selection_mask_path)},
        {QStringLiteral("realPhotoInput"), real_photo_review},
        {QStringLiteral("exactRecipeReopen"), true},
        {QStringLiteral("occludedGlyphPixels"), occluded_glyph_pixels},
        {QStringLiteral("topFirstLayerOrderVerified"), true},
        {QStringLiteral("endpointType"), QStringLiteral("test-adapter-to-real-controller")},
        {QStringLiteral("countsTowardPlan18Runs"), false}};
    auto passed_plan_receipt = tool_plan_receipt;
    passed_plan_receipt.insert(QStringLiteral("dispatchStatus"), QStringLiteral("passed"));
    passed_plan_receipt.insert(QStringLiteral("toolReports"), tool_reports);
    QFile passed_plan_file(tool_plan_path);
    if (!passed_plan_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        passed_plan_file.write(QJsonDocument::fromVariant(passed_plan_receipt).toJson(QJsonDocument::Indented)) <= 0)
      return fail("Persist successful model-proposed tool plan and controller reports");
    QFile receipt_file(QDir(receipt_dir).filePath(QStringLiteral("run-receipt.json")));
    if (!receipt_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        receipt_file.write(QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Indented)) <= 0)
      return fail("Persist live model composition-agent receipt");
    std::cout << "live_composition_agent_tool_run=passed\n"
              << "live_composition_agent_receipt="
              << QDir(receipt_dir).filePath(QStringLiteral("run-receipt.json")).toStdString() << '\n';
    return EXIT_SUCCESS;
  }
  const auto mcp_text = mcp_client.callTool(QStringLiteral("photo.edit_composition"), {
      {QStringLiteral("assetId"), QStringLiteral("composition")},
      {QStringLiteral("operation"), QStringLiteral("add_text")},
      {QStringLiteral("values"), QVariantMap{
          {QStringLiteral("text"), QStringLiteral("MCP FLOAT")},
          {QStringLiteral("x"), 42.0}, {QStringLiteral("y"), 54.0}}}});
  if (!mcp_text.value(QStringLiteral("ok")).toBool())
    std::cerr << "composition MCP text diagnostic: "
              << QJsonDocument::fromVariant(mcp_text).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
  CHECK(mcp_text.value(QStringLiteral("ok")).toBool() &&
      mcp_text.value(QStringLiteral("structuredContent")).toMap()
          .value(QStringLiteral("ok")).toBool(),
      "A typed text edit dispatches through AgentMcpClient into the real composition controller");
  CHECK(!controller->photoLayers().isEmpty() &&
      controller->photoLayers().front().toMap().value(QStringLiteral("text")).toString() ==
          QStringLiteral("MCP FLOAT"),
      "The real controller stores the MCP-created editable text layer");
  const auto unchanged_state = EditorPreviewControllerTestAccess::state(*controller);
  const auto unchanged_layer = unchanged_state.layers.front().toMap();
  const auto unchanged_layer_id = unchanged_layer.value("id").toString();
  const QVariantList already_satisfied_commands{
      QVariantMap{{"operation", "opacity"}, {"values", QVariantMap{{"opacity", 1.0}}}},
      QVariantMap{{"operation", "text"}, {"values", QVariantMap{{"text", "MCP FLOAT"}}}},
      QVariantMap{{"operation", "move"}, {"values", QVariantMap{{"position", 0}}}},
      QVariantMap{{"operation", "transform"}, {"values", QVariantMap{
          {"x", unchanged_layer.value("x")}, {"y", unchanged_layer.value("y")},
          {"scale", unchanged_layer.value("scale", 1.0)}, {"rotation", unchanged_layer.value("rotation", 0.0)}}}},
      QVariantMap{{"operation", "perspective"}, {"values", QVariantMap{{"horizontal", 0.0}, {"vertical", 0.0}}}}};
  for (const auto& value : already_satisfied_commands) {
    auto args = value.toMap();
    args.insert("assetId", "composition");
    args.insert("layerId", unchanged_layer_id);
    const auto result = mcp_client.callTool(QStringLiteral("photo.edit_composition"), args);
    const auto content = result.value("structuredContent").toMap();
    const auto state_after_unchanged = EditorPreviewControllerTestAccess::state(*controller);
    if (!result.value("ok").toBool() || !content.value("alreadySatisfied").toBool() || content.value("changed").toBool() ||
        state_after_unchanged != unchanged_state) {
      QVariantMap differing_layer_fields;
      const auto actual_layer = state_after_unchanged.layers.first().toMap();
      const auto original_layer = unchanged_state.layers.first().toMap();
      for (auto it = actual_layer.cbegin(); it != actual_layer.cend(); ++it)
        if (original_layer.value(it.key()) != it.value()) differing_layer_fields.insert(it.key(),
            QVariantMap{{"before", original_layer.value(it.key())}, {"after", it.value()}});
      std::cerr << "unchanged command diagnostic=" << QJsonDocument::fromVariant(QVariantMap{
          {"operation", args.value("operation")}, {"ok", result.value("ok")}, {"error", result.value("error")},
          {"alreadySatisfied", content.value("alreadySatisfied")}, {"changed", content.value("changed")},
          {"stateEqual", state_after_unchanged == unchanged_state}, {"differingLayerFields", differing_layer_fields}})
          .toJson(QJsonDocument::Compact).toStdString() << '\n';
    }
    CHECK(result.value("ok").toBool() && content.value("alreadySatisfied").toBool() &&
        !content.value("changed").toBool() && EditorPreviewControllerTestAccess::state(*controller) == unchanged_state,
        "A valid already-satisfied composition command must succeed without modifying the recipe");
  }
  for (const auto& args : QVariantList{
      QVariantMap{{"opacity", "1"}}, QVariantMap{{"opacity", 1.0}, {"unsupported", 0.0}}}) {
    const auto invalid = command("opacity", args.toMap(), unchanged_layer_id);
    CHECK(!invalid.value("ok").toBool() && EditorPreviewControllerTestAccess::state(*controller) == unchanged_state,
        "An unchanged target must still reject malformed values");
  }
  const auto missing_unchanged_target = command("opacity", {{"opacity", 1.0}}, "missing-layer");
  CHECK(!missing_unchanged_target.value("ok").toBool(), "Unchanged handling must not accept an unknown layer ID");
  CHECK(controller->setPhotoLayerLocked(0, true), "Lock unchanged test layer");
  const auto unchanged_locked_state = EditorPreviewControllerTestAccess::state(*controller);
  CHECK(!command("opacity", {{"opacity", 1.0}}, unchanged_layer_id).value("ok").toBool() &&
      EditorPreviewControllerTestAccess::state(*controller) == unchanged_locked_state,
      "A matching value must not bypass the layer lock");
  CHECK(controller->setPhotoLayerLocked(0, false), "Unlock unchanged test layer");
  const auto changed_opacity = command("opacity", {{"opacity", 0.5}}, unchanged_layer_id);
  CHECK(changed_opacity.value("ok").toBool() && changed_opacity.value("changed").toBool() &&
      !changed_opacity.value("alreadySatisfied").toBool(), "A real change must remain a mutation");
  CHECK(command("opacity", {{"opacity", 1.0}}, unchanged_layer_id).value("changed").toBool(), "Restore test opacity");
  if (app.arguments().contains(QStringLiteral("--composition-idempotent-contract"))) {
    std::cout << "Five already-satisfied commands succeed without recipe mutation; malformed, missing and locked targets are rejected.\n";
    return EXIT_SUCCESS;
  }
  const QVariantList mcp_lasso_points{
      QVariantMap{{QStringLiteral("x"), 0.25}, {QStringLiteral("y"), 0.20}},
      QVariantMap{{QStringLiteral("x"), 0.70}, {QStringLiteral("y"), 0.20}},
      QVariantMap{{QStringLiteral("x"), 0.70}, {QStringLiteral("y"), 0.60}},
      QVariantMap{{QStringLiteral("x"), 0.25}, {QStringLiteral("y"), 0.60}}};
  const QVariantMap mcp_lasso_values{
      {QStringLiteral("points"), mcp_lasso_points}, {QStringLiteral("feather"), 0.02}};
  const auto mcp_lasso = mcp_client.callTool(QStringLiteral("photo.edit_composition"), {
      {QStringLiteral("assetId"), QStringLiteral("composition")},
      {QStringLiteral("operation"), QStringLiteral("lasso_selection")},
      {QStringLiteral("values"), mcp_lasso_values}});
  const auto lasso_result = mcp_lasso.value(QStringLiteral("structuredContent")).toMap();
  const auto lasso_mask_id = lasso_result.value(QStringLiteral("createdMaskId")).toString();
  CHECK(mcp_lasso.value(QStringLiteral("ok")).toBool() && !lasso_mask_id.isEmpty() &&
      mcp_lasso.value(QStringLiteral("structuredContent")).toMap()
          .value(QStringLiteral("masks")).toList().size() >= 1,
      "The production MCP tool creates a closed lasso selection and returns its stable mask-set ID");
  auto lasso_definition_found = false;
  for (const auto& mask_value : EditorPreviewControllerTestAccess::state(*controller).mask_stack) {
    const auto mask = mask_value.toMap();
    if (mask.value(QStringLiteral("maskSetId")).toString() == lasso_mask_id) {
      lasso_definition_found = mask.value(QStringLiteral("kind")).toString() == QStringLiteral("lasso") &&
          mask.value(QStringLiteral("lassoClosed")).toBool() &&
          mask.value(QStringLiteral("polygonPoints")).toList().size() >= 3 &&
          std::abs(mask.value(QStringLiteral("feather")).toDouble() - 0.02) < 1e-9;
    }
  }
  CHECK(lasso_definition_found, "The MCP lasso result remains a real editable selection mask");
  const auto mcp_lasso_cutout = mcp_client.callTool(QStringLiteral("photo.edit_composition"), {
      {QStringLiteral("assetId"), QStringLiteral("composition")},
      {QStringLiteral("operation"), QStringLiteral("copy_selection")},
      {QStringLiteral("values"), QVariantMap{
          {QStringLiteral("maskId"), lasso_mask_id}, {QStringLiteral("independent"), true}}}});
  if (!mcp_lasso_cutout.value(QStringLiteral("ok")).toBool())
    std::cerr << "MCP lasso cutout diagnostic: "
              << QJsonDocument::fromVariant(mcp_lasso_cutout).toJson(QJsonDocument::Compact).toStdString() << '\n';
  CHECK(mcp_lasso_cutout.value(QStringLiteral("ok")).toBool(),
      "The lasso selection can drive an independent Photo Fragment cutout over the production MCP path");
  QElapsedTimer lasso_materialize_timer;
  lasso_materialize_timer.start();
  while (controller->photoPaintBusy() && lasso_materialize_timer.elapsed() < 20000) {
    QCoreApplication::processEvents();
    QThread::msleep(5);
  }
  const auto lasso_layers = EditorPreviewControllerTestAccess::state(*controller).layers;
  const auto lasso_pixels = lasso_layers.isEmpty() ? QImage{} :
      rasterize_photo_paint_layer(lasso_layers.front().toMap());
  auto has_feathered_lasso_alpha = false;
  if (!lasso_pixels.isNull()) {
    const auto rgba = lasso_pixels.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < rgba.height() && !has_feathered_lasso_alpha; ++y) {
      const auto* row = rgba.constScanLine(y);
      for (int x = 0; x < rgba.width(); ++x) {
        const auto alpha = row[x * 4 + 3];
        if (alpha > 0 && alpha < 255) {
          has_feathered_lasso_alpha = true;
          break;
        }
      }
    }
  }
  if (controller->photoPaintBusy() || !controller->photoPaintError().isEmpty() ||
      lasso_pixels.isNull() || qAlpha(lasso_pixels.pixel(200, 128)) == 0 ||
      qAlpha(lasso_pixels.pixel(40, 40)) != 0 || !has_feathered_lasso_alpha) {
    std::cerr << "lasso cutout diagnostic: size=" << lasso_pixels.width() << 'x'
              << lasso_pixels.height() << " center-alpha="
              << (lasso_pixels.isNull() ? -1 : qAlpha(lasso_pixels.pixel(200, 128)))
              << " outside-alpha="
              << (lasso_pixels.isNull() ? -1 : qAlpha(lasso_pixels.pixel(40, 40)))
              << " feathered=" << has_feathered_lasso_alpha
              << " busy=" << controller->photoPaintBusy()
              << " error=" << controller->photoPaintError().toStdString() << '\n';
  }
  CHECK(!controller->photoPaintBusy() && controller->photoPaintError().isEmpty() &&
      !lasso_pixels.isNull() && qAlpha(lasso_pixels.pixel(200, 128)) > 0 &&
      qAlpha(lasso_pixels.pixel(40, 40)) == 0 && has_feathered_lasso_alpha,
      "Independent MCP cutout pixels follow the feathered lasso and preserve soft and outside alpha");
  controller->undo();
  controller->undo();
  CHECK(controller->photoLayers().front().toMap().value(QStringLiteral("text")).toString() ==
      QStringLiteral("MCP FLOAT"), "Undo removes the independent lasso cutout and selection");
  const auto mcp_paint_layer = mcp_client.callTool(QStringLiteral("photo.edit_composition"), {
      {QStringLiteral("assetId"), QStringLiteral("composition")},
      {QStringLiteral("operation"), QStringLiteral("add_pixel_layer")},
      {QStringLiteral("values"), QVariantMap{}}});
  const auto mcp_paint_layer_result = mcp_paint_layer.value(QStringLiteral("structuredContent")).toMap();
  const auto mcp_paint_layer_id = mcp_paint_layer_result.value(QStringLiteral("createdLayerId")).toString();
  CHECK(mcp_paint_layer.value(QStringLiteral("ok")).toBool() && !mcp_paint_layer_id.isEmpty() &&
      !controller->photoLayers().isEmpty() &&
      controller->photoLayers().front().toMap().value(QStringLiteral("id")).toString() == mcp_paint_layer_id &&
      controller->photoLayers().front().toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("pixel"),
      "MCP can create an editable transparent paint layer and return its actual stable target ID");
  controller->undo();
  const auto mcp_source_layer = mcp_client.callTool(QStringLiteral("photo.edit_composition"), {
      {QStringLiteral("assetId"), QStringLiteral("composition")},
      {QStringLiteral("operation"), QStringLiteral("add_source_layer")},
      {QStringLiteral("values"), QVariantMap{}}});
  const auto mcp_source_layer_result = mcp_source_layer.value(QStringLiteral("structuredContent")).toMap();
  const auto mcp_source_layer_id = mcp_source_layer_result.value(QStringLiteral("createdLayerId")).toString();
  CHECK(mcp_source_layer.value(QStringLiteral("ok")).toBool() && !mcp_source_layer_id.isEmpty() &&
      !controller->photoLayers().isEmpty() &&
      controller->photoLayers().front().toMap().value(QStringLiteral("id")).toString() == mcp_source_layer_id &&
      controller->photoLayers().front().toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("source"),
      "MCP can create an editable linked source layer from the active photograph and return its real ID");
  controller->undo();
  const auto mcp_text_id = controller->photoLayers().front().toMap()
      .value(QStringLiteral("id")).toString();
  const auto mcp_typography = mcp_client.callTool(QStringLiteral("photo.edit_composition"), {
      {QStringLiteral("assetId"), QStringLiteral("composition")},
      {QStringLiteral("operation"), QStringLiteral("typography")},
      {QStringLiteral("layerId"), mcp_text_id},
      {QStringLiteral("values"), QVariantMap{
          {QStringLiteral("family"), QStringLiteral("Helvetica")},
          {QStringLiteral("size"), 44.0}, {QStringLiteral("color"), QStringLiteral("#fff4d6")},
          {QStringLiteral("strokeWidth"), 2.0},
          {QStringLiteral("strokeColor"), QStringLiteral("#ff18202b")}}}});
  if (!mcp_typography.value(QStringLiteral("ok")).toBool())
    std::cerr << "composition typography MCP dispatch failed: "
              << QJsonDocument::fromVariant(mcp_typography).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
  CHECK(mcp_typography.value(QStringLiteral("ok")).toBool(),
      "Typed typography styling dispatches to the real controller over MCP");
  const auto mcp_effect = mcp_client.callTool(QStringLiteral("photo.edit_composition"), {
      {QStringLiteral("assetId"), QStringLiteral("composition")},
      {QStringLiteral("operation"), QStringLiteral("add_effect")},
      {QStringLiteral("layerId"), mcp_text_id},
      {QStringLiteral("values"), QVariantMap{
          {QStringLiteral("effectType"), QStringLiteral("optical_diffusion")},
          {QStringLiteral("radius"), 4.0}, {QStringLiteral("highlightThreshold"), 0.8},
          {QStringLiteral("highlightKnee"), 0.25}, {QStringLiteral("strength"), 0.25},
          {QStringLiteral("warmth"), 0.1}, {QStringLiteral("highlightProtection"), 0.3},
          {QStringLiteral("amount"), 0.35}, {QStringLiteral("enabled"), true}}}});
  CHECK(mcp_effect.value(QStringLiteral("ok")).toBool() &&
      !mcp_effect.value(QStringLiteral("structuredContent")).toMap()
          .value(QStringLiteral("effectId")).toString().isEmpty(),
      "A typed photo-effect edit dispatches through MCP into the real text-layer effect stack");
  const auto initial_effects = controller->photoLayerEffects(0);
  const auto optical_effect_id = initial_effects.isEmpty() ? QString{} :
      initial_effects.front().toMap().value(QStringLiteral("id")).toString();
  QSet<QString> effect_ids;
  CHECK(initial_effects.size() == 1 &&
      initial_effects.front().toMap().value(QStringLiteral("type")).toString() ==
          QStringLiteral("optical_diffusion") &&
      initial_effects.front().toMap().value(QStringLiteral("diffusionRadius")).toDouble() == 4.0 &&
      !optical_effect_id.isEmpty() && optical_effect_id ==
          mcp_effect.value(QStringLiteral("structuredContent")).toMap()
              .value(QStringLiteral("effectId")).toString(),
      "The controller stores typed Optical Diffusion values and returns its stable effect identity");
  const auto composition_inspection = mcp_client.callTool(
      QStringLiteral("photo.inspect_composition"), {
          {QStringLiteral("assetId"), QStringLiteral("composition")}});
  auto inspect_exposes_effect_id = false;
  for (const auto& layer_value : composition_inspection.value(QStringLiteral("structuredContent"))
           .toMap().value(QStringLiteral("layers")).toList()) {
    const auto layer = layer_value.toMap();
    if (layer.value(QStringLiteral("id")).toString() != mcp_text_id) continue;
    for (const auto& effect_value : layer.value(QStringLiteral("effects")).toList())
      inspect_exposes_effect_id = inspect_exposes_effect_id ||
          effect_value.toMap().value(QStringLiteral("id")).toString() == optical_effect_id;
  }
  CHECK(composition_inspection.value(QStringLiteral("ok")).toBool() && inspect_exposes_effect_id,
      "MCP inspection exposes typed effect IDs and controls for later edits");
  effect_ids.insert(optical_effect_id);
  const QVariantList additional_effects{
      QVariantMap{{QStringLiteral("effectType"), QStringLiteral("gaussian_blur")},
          {QStringLiteral("sigma"), 2.0}},
      QVariantMap{{QStringLiteral("effectType"), QStringLiteral("box_blur")},
          {QStringLiteral("radius"), 2}},
      QVariantMap{{QStringLiteral("effectType"), QStringLiteral("motion_blur")},
          {QStringLiteral("distance"), 8.0}, {QStringLiteral("angle"), 30.0}},
      QVariantMap{{QStringLiteral("effectType"), QStringLiteral("posterize_linear_rgb")},
          {QStringLiteral("levels"), 8}},
      QVariantMap{{QStringLiteral("effectType"), QStringLiteral("invert_linear_rgb")}},
      QVariantMap{{QStringLiteral("effectType"), QStringLiteral("unsharp_mask")},
          {QStringLiteral("sigma"), 1.0}, {QStringLiteral("strength"), 1.2},
          {QStringLiteral("threshold"), 0.02}},
      QVariantMap{{QStringLiteral("effectType"), QStringLiteral("imperfect_print")},
          {QStringLiteral("paperScale"), 18.0}, {QStringLiteral("paperTexture"), 0.12},
          {QStringLiteral("inkSpread"), 0.04}, {QStringLiteral("inkDensity"), 0.85},
          {QStringLiteral("redRegistrationX"), 0.35}, {QStringLiteral("blueRegistrationX"), -0.25},
          {QStringLiteral("seed"), 23}},
      QVariantMap{{QStringLiteral("effectType"), QStringLiteral("dragged_shutter")},
          {QStringLiteral("distance"), 12.0}, {QStringLiteral("angle"), 42.0},
          {QStringLiteral("falloff"), 0.55}, {QStringLiteral("highlightWeighting"), 0.3},
          {QStringLiteral("protectSubject"), true}},
  };
  for (const auto& effect_values : additional_effects) {
    const auto effect_call = mcp_client.callTool(QStringLiteral("photo.edit_composition"), {
        {QStringLiteral("assetId"), QStringLiteral("composition")},
        {QStringLiteral("operation"), QStringLiteral("add_effect")},
        {QStringLiteral("layerId"), mcp_text_id},
        {QStringLiteral("values"), effect_values}});
    const auto effect_id = effect_call.value(QStringLiteral("structuredContent")).toMap()
        .value(QStringLiteral("effectId")).toString();
    CHECK(effect_call.value(QStringLiteral("ok")).toBool() && !effect_id.isEmpty() &&
        !effect_ids.contains(effect_id),
        "Every registered layer effect must be addable through typed MCP");
    effect_ids.insert(effect_id);
  }
  const auto state_after_effect = EditorPreviewControllerTestAccess::state(*controller);
  const auto invalid_effect = mcp_client.callTool(QStringLiteral("photo.edit_composition"), {
      {QStringLiteral("assetId"), QStringLiteral("composition")},
      {QStringLiteral("operation"), QStringLiteral("add_effect")},
      {QStringLiteral("layerId"), mcp_text_id},
      {QStringLiteral("values"), QVariantMap{
          {QStringLiteral("effectType"), QStringLiteral("optical_diffusion")},
          {QStringLiteral("radius"), 4.0}, {QStringLiteral("unexpected"), 1.0}}}});
  CHECK(!invalid_effect.value(QStringLiteral("ok")).toBool() &&
      EditorPreviewControllerTestAccess::state(*controller) == state_after_effect,
      "Malformed effect fields must fail without changing the composition");
  const auto text_effects = controller->photoLayerEffects(0);
  QStringList retained_effect_types;
  for (const auto& effect : text_effects)
    retained_effect_types.append(effect.toMap().value(QStringLiteral("type")).toString());
  CHECK(text_effects.size() == 9 && effect_ids.size() == 9 &&
      retained_effect_types.contains(QStringLiteral("gaussian_blur")) &&
      retained_effect_types.contains(QStringLiteral("box_blur")) &&
      retained_effect_types.contains(QStringLiteral("motion_blur")) &&
      retained_effect_types.contains(QStringLiteral("posterize_linear_rgb")) &&
      retained_effect_types.contains(QStringLiteral("invert_linear_rgb")) &&
      retained_effect_types.contains(QStringLiteral("unsharp_mask")) &&
      retained_effect_types.contains(QStringLiteral("optical_diffusion")) &&
      retained_effect_types.contains(QStringLiteral("imperfect_print")) &&
      retained_effect_types.contains(QStringLiteral("dragged_shutter")),
      "The text layer retains all nine registered effects with distinct stable IDs");
  const auto call_effect_operation = [&](const QString& operation, const QVariantMap& values) {
    return mcp_client.callTool(QStringLiteral("photo.edit_composition"), {
        {QStringLiteral("assetId"), QStringLiteral("composition")},
        {QStringLiteral("operation"), operation},
        {QStringLiteral("layerId"), mcp_text_id},
        {QStringLiteral("values"), values}});
  };
  const auto update_diffusion = call_effect_operation(QStringLiteral("update_effect"), {
      {QStringLiteral("effectId"), optical_effect_id},
      {QStringLiteral("radius"), 6.0}, {QStringLiteral("warmth"), 0.3}});
  auto updated_effects = update_diffusion.value(QStringLiteral("structuredContent")).toMap()
      .value(QStringLiteral("effects")).toList();
  auto updated_diffusion = QVariantMap{};
  for (const auto& effect_value : updated_effects) {
    const auto effect = effect_value.toMap();
    if (effect.value(QStringLiteral("id")).toString() == optical_effect_id)
      updated_diffusion = effect;
  }
  CHECK(update_diffusion.value(QStringLiteral("ok")).toBool() &&
      update_diffusion.value(QStringLiteral("structuredContent")).toMap()
          .value(QStringLiteral("effectId")).toString() == optical_effect_id &&
      updated_diffusion.value(QStringLiteral("diffusionRadius")).toDouble() == 6.0 &&
      updated_diffusion.value(QStringLiteral("warmth")).toDouble() == 0.3,
      "MCP updates typed effect controls in place and preserves the effect ID");
  const auto state_after_effect_update = EditorPreviewControllerTestAccess::state(*controller);
  const auto repeated_effect_update = call_effect_operation(QStringLiteral("update_effect"), {
      {QStringLiteral("effectId"), optical_effect_id},
      {QStringLiteral("radius"), 6.0}, {QStringLiteral("warmth"), 0.3}});
  const auto repeated_effect_content = repeated_effect_update.value(QStringLiteral("structuredContent")).toMap();
  CHECK(repeated_effect_update.value(QStringLiteral("ok")).toBool() &&
      repeated_effect_content.value(QStringLiteral("alreadySatisfied")).toBool() &&
      !repeated_effect_content.value(QStringLiteral("changed")).toBool() &&
      repeated_effect_content.value(QStringLiteral("effectId")).toString() == optical_effect_id &&
      EditorPreviewControllerTestAccess::state(*controller) == state_after_effect_update,
      "A validated effect update that is already set must preserve its ID and succeed without mutation");
  const auto wrong_effect_control = call_effect_operation(QStringLiteral("update_effect"), {
      {QStringLiteral("effectId"), optical_effect_id}, {QStringLiteral("paperScale"), 12.0}});
  CHECK(!wrong_effect_control.value(QStringLiteral("ok")).toBool() &&
      EditorPreviewControllerTestAccess::state(*controller) == state_after_effect_update,
      "An effect-specific control cannot mutate a different effect type");
  controller->undo();
  const auto reorder_diffusion = call_effect_operation(QStringLiteral("reorder_effect"), {
      {QStringLiteral("effectId"), optical_effect_id}, {QStringLiteral("position"), 8}});
  const auto reordered_effects = controller->photoLayerEffects(0);
  CHECK(reorder_diffusion.value(QStringLiteral("ok")).toBool() &&
      reordered_effects.size() == 9 &&
      reordered_effects.back().toMap().value(QStringLiteral("id")).toString() == optical_effect_id,
      "MCP reorders an existing effect within its owning layer");
  controller->undo();
  const auto mask_effect = call_effect_operation(QStringLiteral("mask_effect"), {
      {QStringLiteral("effectId"), optical_effect_id}, {QStringLiteral("maskId"), QStringLiteral("subject")}});
  auto masked_effect_id = QString{};
  for (const auto& effect_value : controller->photoLayerEffects(0)) {
    const auto effect = effect_value.toMap();
    if (effect.value(QStringLiteral("id")).toString() == optical_effect_id)
      masked_effect_id = effect.value(QStringLiteral("maskId")).toString();
  }
  CHECK(mask_effect.value(QStringLiteral("ok")).toBool() && !masked_effect_id.isEmpty(),
      "MCP links a selected mask to an existing effect without changing its identity");
  const auto clear_effect_mask = call_effect_operation(QStringLiteral("clear_effect_mask"), {
      {QStringLiteral("effectId"), optical_effect_id}});
  auto cleared_effect_mask = false;
  for (const auto& effect_value : controller->photoLayerEffects(0)) {
    const auto effect = effect_value.toMap();
    if (effect.value(QStringLiteral("id")).toString() == optical_effect_id)
      cleared_effect_mask = effect.value(QStringLiteral("maskId")).toString().isEmpty();
  }
  CHECK(clear_effect_mask.value(QStringLiteral("ok")).toBool() && cleared_effect_mask,
      "MCP clears an existing effect mask through the same effect stack");
  controller->undo();
  controller->undo();
  const auto remove_effect = call_effect_operation(QStringLiteral("remove_effect"), {
      {QStringLiteral("effectId"), optical_effect_id}});
  auto removed_effect_is_absent = true;
  for (const auto& effect_value : controller->photoLayerEffects(0))
    if (effect_value.toMap().value(QStringLiteral("id")).toString() == optical_effect_id)
      removed_effect_is_absent = false;
  CHECK(remove_effect.value(QStringLiteral("ok")).toBool() && removed_effect_is_absent,
      "MCP removes an existing effect by its stable ID");
  controller->undo();
  const auto restored_effects = controller->photoLayerEffects(0);
  CHECK(restored_effects.size() == 9 &&
      restored_effects.front().toMap().value(QStringLiteral("id")).toString() == optical_effect_id,
      "Undo restores the edited, reordered, masked, and removed effect stack exactly");
  const auto text_id = mcp_text_id;
  CHECK(command("typography", {{"size", 88.0}, {"color", "#ffffdd"}, {"weight", 700},
      {"strokeWidth", 3.0}, {"strokeColor", "#ff203050"}, {"shadowX", 5.0}, {"shadowY", 6.0}}, text_id).value("ok").toBool(), "Agent style text");
  const auto text_state = EditorPreviewControllerTestAccess::state(*controller);
  const auto text_output = render(text_state);
  CHECK(command("copy_selection", {{"maskId", "subject"}}).value("ok").toBool(), "Copy masked base pixels");
  const auto cutout_state = EditorPreviewControllerTestAccess::state(*controller);
  CHECK(cutout_state.layers.size() == 2, "Two independent layers");
  const auto composite = render(cutout_state);
  auto base_state = cutout_state; base_state.layers.clear();
  const auto base = render(base_state);
  CHECK(!composite.isNull() && composite != text_output, "Cutout must visibly occlude text");
  int hidden_text_pixels = 0;
  for (int y = 0; y < source.height(); ++y) for (int x = 0; x < source.width(); ++x) {
    if (x > 213 && x < 266) {
      CHECK(composite.pixelColor(x, y) == base.pixelColor(x, y), "Subject pixels must cover the text without recoloring");
      if (text_output.pixelColor(x, y) != base.pixelColor(x, y)) ++hidden_text_pixels;
    } else if (x < 207 || x > 273) {
      CHECK(composite.pixelColor(x, y) == text_output.pixelColor(x, y), "Outside selection must retain text");
    }
  }
  CHECK(hidden_text_pixels > 100, "Fixture must exercise text behind subject");
  CHECK(validate_edit_recipe_json(serialize_edit_recipe_json(cutout_state)).valid, "Styled cutout recipe validates");
  CHECK(render(parse_edit_recipe_json(serialize_edit_recipe_json(cutout_state))) == composite,
      "Save/reopen must preserve masked text composition exactly");
  controller->undo();
  CHECK(render(EditorPreviewControllerTestAccess::state(*controller)) == text_output, "Undo cutout");
  controller->redo();
  CHECK(render(EditorPreviewControllerTestAccess::state(*controller)) == composite, "Redo cutout");
  CHECK(command("move", {{"position", 0}}, text_id).value("ok").toBool(), "Agent reorder");
  CHECK(render(EditorPreviewControllerTestAccess::state(*controller)) == text_output, "Moving text above subject exposes text");
  const auto independent_command = command("copy_selection", {{"maskId", "subject"}, {"independent", true}});
  const auto independent_created_id = independent_command.value("createdLayerId").toString();
  CHECK(independent_command.value("ok").toBool() && independent_command.value("paintBusy").toBool() &&
      !independent_created_id.isEmpty() &&
      independent_created_id == EditorPreviewControllerTestAccess::state(*controller).layers.front().toMap()
          .value("id").toString(),
      "Agent can queue an independent cutout and receive its stable layer ID through the shared composition operation");
  QElapsedTimer materialize_timer; materialize_timer.start();
  while (controller->photoPaintBusy() && materialize_timer.elapsed() < 20000) {
    QCoreApplication::processEvents(); QThread::msleep(5);
  }
  CHECK(!controller->photoPaintBusy() && controller->photoPaintError().isEmpty(),
      "Agent independent cutout materializes without an asynchronous error: " +
          controller->photoPaintError().toStdString());
  const auto preset_saved = command("save_photo_fragment_preset", {
      {"name", "Integer Rotation Test"}, {"tipLayerId", independent_created_id},
      {"rotationVariation", qlonglong{8}}, {"scaleVariation", 0.08}});
  CHECK(preset_saved.value("ok").toBool() &&
      preset_saved.value("photoFragmentBrushPresets").toList().contains(
          QStringLiteral("Integer Rotation Test")),
      "Agent Photo Fragment presets accept integer rotation variation and return the saved preset name");
  const auto independent_layers = EditorPreviewControllerTestAccess::state(*controller).layers;
  CHECK(independent_layers.front().toMap().value("kind").toString() == "pixel" &&
      !independent_layers.front().toMap().value("copyBaseImage").toBool() &&
      !independent_layers.front().toMap().value("selectionScoped").toBool(),
      "Agent independent cutout is a self-contained pixel layer");
  const auto independent_pixels = rasterize_photo_paint_layer(independent_layers.front().toMap());
  CHECK(!independent_pixels.isNull() && qAlpha(independent_pixels.pixel(240, 160)) == 255 &&
      qAlpha(independent_pixels.pixel(100, 160)) == 0,
      "Agent cutout stores selection alpha in its own raster pixels");
  const auto independent_state = EditorPreviewControllerTestAccess::state(*controller);
  const auto independent_render = render(independent_state);
  CHECK(validate_edit_recipe_json(serialize_edit_recipe_json(independent_state)).valid &&
      render(parse_edit_recipe_json(serialize_edit_recipe_json(independent_state))) == independent_render,
      "Independent cutout pixels survive recipe save/reopen exactly");
  EditRecipeState layered_mask_state;
  QByteArray source_mask(source.width() * source.height(), char(0));
  QByteArray linked_selection(source.width() * source.height(), char(0));
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      if (x < 230) source_mask[y * source.width() + x] = char(255);
      if (x < 240) linked_selection[y * source.width() + x] = char(255);
    }
  }
  auto active_subject = alpha;
  layered_mask_state.layers = {QVariantMap{{"id", "already-masked-fill"}, {"kind", "fill"},
      {"fillColor", "#f01040"}, {"maskData", source_mask}, {"maskWidth", source.width()},
      {"maskHeight", source.height()}, {"selectionScoped", true},
      {"selectionMaskSetId", "left-selection"}, {"selectionMaskSetName", "Left"},
      {"selectionMaskSetIds", QVariantList{"left-selection"}},
      {"selectionMaskSetNames", QVariantList{"Left"}}}};
  layered_mask_state.mask_stack = {
      QVariantMap{{"id", "left-component"}, {"maskSetId", "left-selection"},
          {"maskSetName", "Left"}, {"toolRole", "selection"}, {"kind", "brush"},
          {"enabled", true}, {"operation", "add"}, {"segmentationWidth", source.width()},
          {"segmentationHeight", source.height()}, {"segmentationData", linked_selection}},
      QVariantMap{{"id", "subject-component"}, {"maskSetId", "subject"},
          {"maskSetName", "Subject"}, {"toolRole", "selection"}, {"kind", "brush"},
          {"enabled", true}, {"operation", "add"}, {"segmentationWidth", source.width()},
          {"segmentationHeight", source.height()}, {"segmentationData", active_subject}}};
  EditorPreviewImageProvider layered_provider;
  auto layered_controller = std::make_unique<EditorPreviewController>();
  layered_controller->setImageProvider(&layered_provider);
  layered_controller->loadAsset("layered-mask-copy", path, {}, {}, {}, {},
      serialize_edit_recipe_json(layered_mask_state));
  QElapsedTimer layered_timer; layered_timer.start();
  while ((layered_controller->displayPreviewSource().isEmpty() || layered_controller->processing()) &&
      layered_timer.elapsed() < 20000) {
    QCoreApplication::processEvents(); QThread::msleep(5);
  }
  CHECK(!layered_controller->displayPreviewSource().isEmpty(), "Load already-masked layer fixture");
  CHECK(layered_controller->selectMaskSet("subject"), "Select intersecting mask");
  const auto masked_copy_command = layered_controller->photoCompositionCommand(
      {{"assetId", "layered-mask-copy"}, {"operation", "copy_selection"},
       {"layerId", "already-masked-fill"},
       {"values", QVariantMap{{"maskId", "subject"}}}});
  CHECK(masked_copy_command.value("ok").toBool(),
      "Copy an already-masked content layer through an additional selection");
  CHECK(layered_controller->photoLayers().size() == 2 &&
      layered_controller->photoLayers().front().toMap().value("selectionIntersectionMaskSetIds").toList().contains("subject"),
      "Copied layer preserves its linked mask and records the active selection as an intersection");
  CHECK(layered_controller->setPhotoLayerEnabled(1, false), "Hide the original masked source layer");
  const auto masked_copy_image = render(EditorPreviewControllerTestAccess::state(*layered_controller));
  const auto masked_copy_base = render(EditRecipeState{});
  const auto intersection_pixel = masked_copy_image.pixelColor(220, 160);
  CHECK(intersection_pixel.red() > 180 && intersection_pixel.green() < 40 && intersection_pixel.blue() < 70 &&
      masked_copy_image.pixelColor(235, 160) == masked_copy_base.pixelColor(235, 160) &&
      masked_copy_image.pixelColor(100, 160) == masked_copy_base.pixelColor(100, 160) &&
      masked_copy_image.pixelColor(250, 160) == masked_copy_base.pixelColor(250, 160),
      "Copied layer pixels are visible only in the intersection of direct, linked, and active masks");
  const auto linked_intersection_state = EditorPreviewControllerTestAccess::state(*layered_controller);
  CHECK(validate_edit_recipe_json(serialize_edit_recipe_json(linked_intersection_state)).valid &&
      render(parse_edit_recipe_json(serialize_edit_recipe_json(linked_intersection_state))) == masked_copy_image,
      "Linked mask intersections survive recipe save/reopen with identical rendering");
  auto isolated_layer_state = linked_intersection_state;
  isolated_layer_state.layers = {linked_intersection_state.layers.front()};
  QImage transparent_base(source.size(), QImage::Format_RGBA8888);
  transparent_base.setColorSpace(QColorSpace(QColorSpace::SRgb));
  transparent_base.fill(Qt::transparent);
  const auto isolated_layer = render_output_image_with_backend(transparent_base, isolated_layer_state,
      QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
  CHECK(!isolated_layer.isNull() && qAlpha(isolated_layer.pixel(220, 160)) > 0 &&
      qAlpha(isolated_layer.pixel(235, 160)) == 0 && qAlpha(isolated_layer.pixel(100, 160)) == 0,
      "The renderer can resolve a masked content layer onto transparency for independent rasterization");
  const auto snapshot_pixels = render_photo_layer_snapshot(source, linked_intersection_state,
      linked_intersection_state.layers.front().toMap().value("id").toString());
  CHECK(!snapshot_pixels.isNull() && qAlpha(snapshot_pixels.pixel(220, 160)) > 0 &&
      qAlpha(snapshot_pixels.pixel(235, 160)) == 0 && qAlpha(snapshot_pixels.pixel(100, 160)) == 0,
      "Snapshot rendering retains the source RGB while producing transparent masked output");
  const auto linked_layer_id = linked_intersection_state.layers.front().toMap().value("id").toString();
  const auto independent_layer_command = layered_controller->photoCompositionCommand(
      {{"assetId", "layered-mask-copy"}, {"operation", "copy_selection"},
       {"layerId", linked_layer_id},
       {"values", QVariantMap{{"maskId", "subject"}, {"independent", true}}}});
  CHECK(independent_layer_command.value("ok").toBool() && independent_layer_command.value("paintBusy").toBool(),
      "Agent can queue an independent content-layer cutout");
  QElapsedTimer layer_snapshot_timer; layer_snapshot_timer.start();
  while (layered_controller->photoPaintBusy() && layer_snapshot_timer.elapsed() < 20000) {
    QCoreApplication::processEvents(); QThread::msleep(5);
  }
  CHECK(!layered_controller->photoPaintBusy() && layered_controller->photoPaintError().isEmpty(),
      "Independent content-layer cutout rasterizes successfully: " +
          layered_controller->photoPaintError().toStdString());
  const auto independent_layer_state = EditorPreviewControllerTestAccess::state(*layered_controller);
  const auto independent_layer = independent_layer_state.layers.front().toMap();
  CHECK(independent_layer.value("kind").toString() == "pixel" &&
      !independent_layer.contains("selectionScoped") && !independent_layer.contains("maskData") &&
      !independent_layer.contains("selectionMaskSetIds") &&
      independent_layer.value("paintTiles").toMap().size() > 0,
      "Independent content cutout stores pixels without live mask or source-layer links");
  const auto independent_content_pixels = rasterize_photo_paint_layer(independent_layer);
  CHECK(!independent_content_pixels.isNull() &&
      qAlpha(independent_content_pixels.pixel(220, 160)) > 0 &&
      qAlpha(independent_content_pixels.pixel(235, 160)) == 0 &&
      qAlpha(independent_content_pixels.pixel(100, 160)) == 0,
      "Independent content pixels contain only the direct-mask and selection intersection");
  const auto independent_layer_image = render(independent_layer_state);
  CHECK(independent_layer_image == masked_copy_image,
      "Independent content cutout preserves the linked masked copy's rendered appearance");
  CHECK(validate_edit_recipe_json(serialize_edit_recipe_json(independent_layer_state)).valid &&
      render(parse_edit_recipe_json(serialize_edit_recipe_json(independent_layer_state))) == independent_layer_image,
      "Independent content pixels survive recipe save/reopen exactly");
  layered_controller->undo();
  CHECK(layered_controller->photoLayers().size() == 2,
      "Undo removes the independent copy in one step");
  layered_controller->redo();
  CHECK(layered_controller->photoLayers().size() == 3 &&
      render(EditorPreviewControllerTestAccess::state(*layered_controller)) == independent_layer_image,
      "Redo restores the complete independent content snapshot");
  CHECK(!controller->photoCompositionCommand({{"assetId", "wrong"}, {"operation", "add_text"}}).value("ok").toBool(), "Reject stale asset");
  QString variable_font_family;
  QVariantList variable_font_axes;
  for (const auto& family : controller->availablePhotoTextFonts()) {
    const auto axes = controller->availablePhotoTextAxes(family, {});
    if (!axes.isEmpty()) {
      variable_font_family = family;
      variable_font_axes = axes;
      break;
    }
  }
  CHECK(!variable_font_family.isEmpty(), "The registered font set includes a real variable face for axis inspection");
  const auto font_inspection = controller->photoCompositionCommand({
      {"assetId", "composition"}, {"operation", "inspect"},
      {"values", QVariantMap{{"family", variable_font_family}}}});
  CHECK(font_inspection.value("ok").toBool() &&
      font_inspection.value("fontFamily").toString() == variable_font_family &&
      font_inspection.value("fontAxes").toList() == variable_font_axes &&
      !font_inspection.value("fontAxes").toList().isEmpty(),
      "MCP inspection exposes the chosen variable face's real OpenType axis tags and ranges");
  CHECK(!controller->photoCompositionCommand({
      {"assetId", "composition"}, {"operation", "inspect"},
      {"values", QVariantMap{{"family", variable_font_family}, {"seed", 10}}}})
          .value("ok").toBool(),
      "Font-axis inspection rejects unrelated values rather than silently ignoring them");
  const auto text_layer_index = [&]() {
    const auto layers = controller->photoLayers();
    for (qsizetype index = 0; index < layers.size(); ++index)
      if (layers.at(index).toMap().value("id").toString() == text_id) return static_cast<int>(index);
    return -1;
  }();
  CHECK(text_layer_index >= 0 && controller->setPhotoLayerLocked(text_layer_index, true),
      "Lock the explicit agent target for locked-target coverage");
  const auto locked_state = EditorPreviewControllerTestAccess::state(*controller);
  CHECK(!command("typography", {{"strokeWidth", 3.0}}, text_id).value("ok").toBool() &&
      EditorPreviewControllerTestAccess::state(*controller).layers == locked_state.layers,
      "A locked explicit target rejects the command without mutating another layer");
  CHECK(controller->setPhotoLayerLocked(text_layer_index, false), "Unlock text target after rejection test");
  EditorPreviewControllerTestAccess::clearHistory(*controller, QStringLiteral("composition"));
  const auto mixed_history_baseline = EditorPreviewControllerTestAccess::state(*controller);
  const auto mixed_history_baseline_recipe = serialize_edit_recipe_json(mixed_history_baseline);
  const auto mixed_history_start_index = controller->editHistoryCurrentIndex();
  for (int operation = 0; operation < 100; ++operation) {
    const bool typography_operation = operation % 2 == 0;
    const auto sequence_index = operation / 2 + 1;
    const auto stroke_width = sequence_index * 0.25;
    const auto x = sequence_index * 1.0;
    const auto result = typography_operation
        ? command("typography", {{"strokeWidth", stroke_width}}, text_id)
        : command("transform", {{"x", x}, {"y", -x}, {"scale", 1.0 + sequence_index * 0.005},
              {"rotation", sequence_index * 3.0}}, text_id);
    CHECK(result.value("ok").toBool(), "Each mixed-history text operation is accepted");
    const auto layer_index = [&]() {
      const auto layers = controller->photoLayers();
      for (qsizetype index = 0; index < layers.size(); ++index)
        if (layers.at(index).toMap().value("id").toString() == text_id) return static_cast<int>(index);
      return -1;
    }();
    CHECK(layer_index >= 0, "The explicit text target remains available through the mixed sequence");
    if (typography_operation) {
      CHECK(std::abs(controller->photoLayerTypography(layer_index).value("strokeWidth").toDouble() -
          stroke_width) < 1e-9,
          "Typography operations update the explicitly targeted text layer");
    } else {
      const auto row = controller->photoLayers().at(layer_index).toMap();
      CHECK(std::abs(row.value("x").toDouble() - x) < 1e-9 &&
          std::abs(row.value("y").toDouble() + x) < 1e-9,
          "Transform operations update the same explicit text target");
    }
  }
  const auto mixed_history_final = EditorPreviewControllerTestAccess::state(*controller);
  const auto mixed_history_final_recipe = serialize_edit_recipe_json(mixed_history_final);
  const auto mixed_history_end_index = controller->editHistoryCurrentIndex();
  CHECK(mixed_history_end_index - mixed_history_start_index == 100,
      "The mixed sequence creates exactly one undo entry per operation");
  const auto mixed_history_final_image = render(mixed_history_final);
  for (int operation = 0; operation < 100; ++operation) controller->undo();
  CHECK(controller->editHistoryCurrentIndex() == mixed_history_start_index &&
      serialize_edit_recipe_json(EditorPreviewControllerTestAccess::state(*controller)) == mixed_history_baseline_recipe,
      "Undoing the complete 100-operation sequence restores canonical recipe state exactly");
  for (int operation = 0; operation < 100; ++operation) controller->redo();
  const auto mixed_history_redone = EditorPreviewControllerTestAccess::state(*controller);
  CHECK(controller->editHistoryCurrentIndex() == mixed_history_end_index &&
      serialize_edit_recipe_json(mixed_history_redone) == mixed_history_final_recipe &&
      render(mixed_history_redone) == mixed_history_final_image,
      "Redoing the complete 100-operation sequence restores state and pixels exactly");
  CHECK(!command("typography", {{"strokeWidth", -1.0}}, text_id).value("ok").toBool(), "Reject invalid stroke");
  CHECK(!command("transform", {{"x", "oops"}, {"y", 0}, {"scale", 1.0}, {"rotation", 0.0}}, text_id).value("ok").toBool(), "Reject invalid geometry");
  const auto evidence = qEnvironmentVariable("PHOTO_COMPOSITION_EVIDENCE");
  if (!evidence.isEmpty()) {
    CHECK(QDir().mkpath(evidence), "Evidence directory");
    CHECK(composite.save(evidence + "/text-behind-mask.png"), "Save composite");
    CHECK(text_output.save(evidence + "/text-above-mask.png"), "Save comparison");
  }
  std::cout << "top_first_fixtures=" << ordering_fixtures
            << " locked_target_rejection=passed\n"
            << "mixed_history_undo_redo_operations=100\n"
            << "Top-first order, groups, styled text, agent composition, cutout coverage, undo/redo, and recipe round-trip passed\n";
}
