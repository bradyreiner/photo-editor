#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QCoreApplication>
#include <QColor>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QVector>

#include "agent_edit_recipe_patch.h"
#include "agent_edit_style.h"
#include "agent_local_model.h"
#include "agent_runtime_paths.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QVariantMap style_by_id(const QString& id) {
  const auto styles = photo_editor::desktop::built_in_agent_edit_styles();
  for (const auto& style_value : styles) {
    const auto style = style_value.toMap();
    if (style.value(QStringLiteral("id")).toString() == id) {
      return style;
    }
  }
  return {};
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

QVariantMap model_by_id(const QVariantList& models, const QString& id) {
  for (const auto& model_value : models) {
    const auto model = model_value.toMap();
    if (model.value(QStringLiteral("id")).toString() == id) {
      return model;
    }
  }
  return {};
}

QJsonObject read_json_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QJsonDocument::fromJson(file.readAll()).object();
}

class FakeOllamaServer final {
 public:
  FakeOllamaServer() {
    server_ = new QTcpServer(QCoreApplication::instance());
    QObject::connect(server_, &QTcpServer::newConnection, server_, [this]() { acceptConnections(); });
  }

  [[nodiscard]] bool start() {
    return server_->listen(QHostAddress::LocalHost, 0) ||
           server_->listen(QHostAddress::AnyIPv4, 0);
  }

  [[nodiscard]] QString endpoint() const {
    return QStringLiteral("http://127.0.0.1:%1").arg(server_->serverPort());
  }

  void setResponse(const QString& path, const QJsonObject& response) {
    responses_.insert(path, response);
    response_statuses_.insert(path, 200);
    chunked_paths_.remove(path);
  }

  void setErrorResponse(const QString& path, int status_code, const QJsonObject& response) {
    responses_.insert(path, response);
    response_statuses_.insert(path, status_code);
    chunked_paths_.remove(path);
  }

  void setChunkedResponse(const QString& path, const QJsonObject& response) {
    responses_.insert(path, response);
    response_statuses_.insert(path, 200);
    chunked_paths_.insert(path);
  }

  [[nodiscard]] QJsonObject lastRequestForPath(const QString& path) const {
    for (auto index = requests_.size() - 1; index >= 0; --index) {
      const auto request = requests_.at(index);
      if (request.value(QStringLiteral("path")).toString() == path) {
        return request;
      }
    }
    return {};
  }

 private:
  void acceptConnections() {
    while (server_->hasPendingConnections()) {
      auto* socket = server_->nextPendingConnection();
      socket->setParent(server_);
      socket->setProperty("buffer", QByteArray{});
      QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() { readRequest(socket); });
      QObject::connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
  }

  void readRequest(QTcpSocket* socket) {
    auto buffer = socket->property("buffer").toByteArray();
    buffer.append(socket->readAll());
    const auto header_end = buffer.indexOf("\r\n\r\n");
    if (header_end < 0) {
      socket->setProperty("buffer", buffer);
      return;
    }

    const auto header = buffer.left(header_end);
    const auto lines = header.split('\n');
    const auto request_line = QString::fromLatin1(lines.value(0)).trimmed();
    const auto request_parts = request_line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const auto path = request_parts.size() >= 2 ? request_parts.at(1) : QString{};
    auto content_length = -1;
    for (const auto& line_bytes : lines) {
      const auto line = QString::fromLatin1(line_bytes).trimmed();
      if (line.toLower().startsWith(QStringLiteral("content-length:"))) {
        auto ok = false;
        content_length = line.mid(QStringLiteral("content-length:").size()).trimmed().toInt(&ok);
        if (!ok) {
          content_length = -1;
        }
      }
    }
    if (content_length < 0) {
      writeResponse(socket, 411, QJsonObject{{QStringLiteral("error"), QStringLiteral("content length required")}});
      return;
    }

    const auto body_start = header_end + 4;
    if (buffer.size() - body_start < content_length) {
      socket->setProperty("buffer", buffer);
      return;
    }
    const auto document = QJsonDocument::fromJson(buffer.mid(body_start, content_length));
    auto request = document.object();
    request.insert(QStringLiteral("path"), path);
    requests_.append(request);

    if (!responses_.contains(path)) {
      writeResponse(socket, 404, QJsonObject{{QStringLiteral("error"), QStringLiteral("missing fake response")}});
      return;
    }
    if (chunked_paths_.contains(path)) {
      writeChunkedResponse(socket, responses_.value(path));
      return;
    }
    writeResponse(socket, response_statuses_.value(path, 200), responses_.value(path));
  }

  static void writeChunkedResponse(QTcpSocket* socket, const QJsonObject& response) {
    const auto body = QJsonDocument(response).toJson(QJsonDocument::Compact);
    const auto split = std::max<qsizetype>(1, body.size() / 2);
    const auto first = body.left(split);
    const auto second = body.mid(split);
    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n");
    for (const auto& chunk : {first, second}) {
      if (chunk.isEmpty()) {
        continue;
      }
      socket->write(QByteArray::number(chunk.size(), 16));
      socket->write("\r\n");
      socket->write(chunk);
      socket->write("\r\n");
    }
    socket->write("0\r\n\r\n");
    socket->flush();
    socket->disconnectFromHost();
  }

  static void writeResponse(QTcpSocket* socket, int status_code, const QJsonObject& response) {
    const auto body = QJsonDocument(response).toJson(QJsonDocument::Compact);
    QByteArray header;
    header.append("HTTP/1.1 ");
    header.append(QByteArray::number(status_code));
    header.append(status_code == 200 ? " OK" : " Error");
    header.append("\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: ");
    header.append(QByteArray::number(body.size()));
    header.append("\r\n\r\n");
    socket->write(header);
    socket->write(body);
    socket->flush();
    socket->disconnectFromHost();
  }

  QTcpServer* server_ = nullptr;
  QMap<QString, QJsonObject> responses_;
  QMap<QString, int> response_statuses_;
  QSet<QString> chunked_paths_;
  QVector<QJsonObject> requests_;
};

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("AgentLocalModelTest"));

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("temporary runtime directory should be created");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());

  const auto models = photo_editor::desktop::available_agent_local_models();
  if (models.isEmpty() ||
      models.first().toMap().value(QStringLiteral("id")).toString() !=
          photo_editor::desktop::default_agent_local_model_id()) {
    return fail("local model registry should expose the deterministic rulebook model");
  }
  const auto deterministic_model = models.first().toMap();
  if (deterministic_model.value(QStringLiteral("status")).toString() != QStringLiteral("available") ||
      !deterministic_model.value(QStringLiteral("available")).toBool() ||
      deterministic_model.value(QStringLiteral("configured")).toBool()) {
    return fail("built-in local model should expose available status metadata");
  }

  const auto models_directory = QDir(runtime_root.path()).filePath(QStringLiteral("agent-models"));
  if (!QDir().mkpath(models_directory)) {
    return fail("configured model directory should be created");
  }
  const auto configured_model_path = QDir(runtime_root.path()).filePath(QStringLiteral("models/local-model.bin"));
  if (!QDir().mkpath(QFileInfo(configured_model_path).dir().absolutePath())) {
    return fail("configured model artifact directory should be created");
  }
  FakeOllamaServer fake_ollama_server;
  const auto fake_ollama_server_available = fake_ollama_server.start();
  if (fake_ollama_server_available) {
    fake_ollama_server.setResponse(
        QStringLiteral("/api/show"),
        QJsonObject{
            {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
            {QStringLiteral("details"),
             QJsonObject{
                 {QStringLiteral("family"), QStringLiteral("qwen2")},
                 {QStringLiteral("parameter_size"), QStringLiteral("7B")},
                 {QStringLiteral("quantization_level"), QStringLiteral("Q4_K_M")},
             }},
        });
  }
  QFile configured_model_file(configured_model_path);
  if (!configured_model_file.open(QIODevice::WriteOnly)) {
    return fail("configured model artifact should be writable");
  }
  configured_model_file.write("local model placeholder");
  configured_model_file.close();
  QFile manifest_file(QDir(models_directory).filePath(QStringLiteral("models.json")));
  if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return fail("configured model manifest should be writable");
  }
  manifest_file.write(R"json({
    "models": [
      {
        "id": "manifest-local",
        "name": "Manifest Local",
        "backend": "local",
        "path": "models/local-model.bin",
        "status": "loading",
        "description": "Configured local model metadata"
      },
      {
        "id": "missing-local",
        "name": "Missing Local",
        "backend": "local",
        "path": "models/missing.bin"
      },
      {
        "id": "rulebook-deterministic",
        "name": "Duplicate Built In",
        "backend": "local"
      },
      {
        "id": "cloud-default-blocked",
        "name": "Cloud Default Blocked",
        "backend": "cloud"
      },
      {
        "id": "cloud-explicit-opt-in",
        "name": "Cloud Explicit Opt In",
        "backend": "cloud",
        "allowCloud": true
      },
      {
        "id": "ollama-qwen-test",
        "name": "Ollama Qwen Test",
        "backend": "ollama",
        "endpoint": ")json");
  manifest_file.write(
      fake_ollama_server_available ? fake_ollama_server.endpoint().toUtf8()
                                   : QByteArrayLiteral("http://127.0.0.1:11434"));
  manifest_file.write(R"json(",
        "model": "qwen2.5vl:7b",
        "vision": true,
        "requestTimeoutMs": 45000,
        "contextWindow": 16384,
        "maxImageEdge": 1024,
        "status": "available",
        "description": "Fake Ollama test model"
      }
    ]
  })json");
  manifest_file.close();
  const auto discovered_models = photo_editor::desktop::available_agent_local_models();
  const auto manifest_model = model_by_id(discovered_models, QStringLiteral("manifest-local"));
  if (manifest_model.isEmpty() ||
      manifest_model.value(QStringLiteral("status")).toString() != QStringLiteral("loading") ||
      manifest_model.value(QStringLiteral("available")).toBool() ||
      !manifest_model.value(QStringLiteral("configured")).toBool() ||
      manifest_model.value(QStringLiteral("path")).toString().isEmpty()) {
    return fail("configured local model manifest should expose loading status metadata");
  }
  const auto missing_model = model_by_id(discovered_models, QStringLiteral("missing-local"));
  if (missing_model.isEmpty() ||
      missing_model.value(QStringLiteral("status")).toString() != QStringLiteral("failed") ||
      missing_model.value(QStringLiteral("available")).toBool() ||
      missing_model.value(QStringLiteral("statusReason")).toString().isEmpty()) {
    return fail("configured local model with missing path should be reported as failed");
  }
  const auto duplicate_model = model_by_id(discovered_models, QStringLiteral("rulebook-deterministic"));
  if (duplicate_model.value(QStringLiteral("configured")).toBool()) {
    return fail("configured local model should not override a built-in model id");
  }
  const auto cloud_default_blocked =
      model_by_id(discovered_models, QStringLiteral("cloud-default-blocked"));
  if (cloud_default_blocked.isEmpty() ||
      cloud_default_blocked.value(QStringLiteral("status")).toString() != QStringLiteral("failed") ||
      cloud_default_blocked.value(QStringLiteral("available")).toBool() ||
      !cloud_default_blocked.value(QStringLiteral("requiresCloudOptIn")).toBool() ||
      cloud_default_blocked.value(QStringLiteral("cloudManifestOptIn")).toBool() ||
      cloud_default_blocked.value(QStringLiteral("cloudEnvironmentOptIn")).toBool()) {
    return fail("configured cloud models should be unavailable unless the manifest explicitly opts in");
  }
  const auto cloud_without_environment =
      model_by_id(discovered_models, QStringLiteral("cloud-explicit-opt-in"));
  if (cloud_without_environment.isEmpty() ||
      cloud_without_environment.value(QStringLiteral("status")).toString() != QStringLiteral("failed") ||
      cloud_without_environment.value(QStringLiteral("available")).toBool() ||
      !cloud_without_environment.value(QStringLiteral("cloudManifestOptIn")).toBool() ||
      cloud_without_environment.value(QStringLiteral("cloudEnvironmentOptIn")).toBool()) {
    return fail("configured cloud models should also require an environment opt-in");
  }
  qputenv("PHOTO_EDITOR_ENABLE_CLOUD_MODELS", QByteArrayLiteral("1"));
  const auto cloud_opted_in_models = photo_editor::desktop::available_agent_local_models();
  const auto cloud_with_environment =
      model_by_id(cloud_opted_in_models, QStringLiteral("cloud-explicit-opt-in"));
  if (cloud_with_environment.isEmpty() ||
      cloud_with_environment.value(QStringLiteral("status")).toString() != QStringLiteral("unavailable") ||
      cloud_with_environment.value(QStringLiteral("available")).toBool() ||
      !cloud_with_environment.value(QStringLiteral("cloudManifestOptIn")).toBool() ||
      !cloud_with_environment.value(QStringLiteral("cloudEnvironmentOptIn")).toBool() ||
      cloud_with_environment.value(QStringLiteral("statusReason")).toString().isEmpty()) {
    return fail("explicit cloud opt-in should be visible but should not enable an unimplemented cloud runner");
  }
  qunsetenv("PHOTO_EDITOR_ENABLE_CLOUD_MODELS");

  const auto ollama_model = model_by_id(discovered_models, QStringLiteral("ollama-qwen-test"));
  if (ollama_model.isEmpty() ||
      ollama_model.value(QStringLiteral("backend")).toString() != QStringLiteral("ollama") ||
      ollama_model.value(QStringLiteral("model")).toString() != QStringLiteral("qwen2.5vl:7b") ||
      !ollama_model.value(QStringLiteral("vision")).toBool() ||
      ollama_model.value(QStringLiteral("requestTimeoutMs")).toInt() != 45000 ||
      ollama_model.value(QStringLiteral("contextWindow")).toInt() != 16384 ||
      ollama_model.value(QStringLiteral("maxImageEdge")).toInt() != 1024 ||
      !ollama_model.value(QStringLiteral("available")).toBool() ||
      ollama_model.value(QStringLiteral("requiresCloudOptIn")).toBool()) {
    return fail("configured Ollama models should be discovered as local available backends");
  }
  if (photo_editor::desktop::default_agent_local_model_id() != QStringLiteral("ollama-qwen-test")) {
    return fail("the first available configured Ollama model should be the real default instead of the deterministic fallback");
  }
  if (!fake_ollama_server_available) {
    qputenv(
        "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
        QByteArrayLiteral(R"json({"model":"qwen2.5vl:7b","details":{"family":"qwen2","parameter_size":"7B","quantization_level":"Q4_K_M"}})json"));
  }
  const auto ollama_probe = photo_editor::desktop::probe_agent_local_model(QStringLiteral("ollama-qwen-test"));
  const auto show_request = fake_ollama_server_available
                                ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/show"))
                                : QJsonObject{{QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")}};
  if (!ollama_probe.value(QStringLiteral("ok")).toBool() ||
      !ollama_probe.value(QStringLiteral("available")).toBool() ||
      ollama_probe.value(QStringLiteral("status")).toString() != QStringLiteral("available") ||
      ollama_probe.value(QStringLiteral("modelFamily")).toString() != QStringLiteral("qwen2") ||
      ollama_probe.value(QStringLiteral("parameterSize")).toString() != QStringLiteral("7B") ||
      ollama_probe.value(QStringLiteral("quantizationLevel")).toString() != QStringLiteral("Q4_K_M") ||
      show_request.value(QStringLiteral("model")).toString() != QStringLiteral("qwen2.5vl:7b")) {
    return fail("Ollama probe should call the local show API and expose model details");
  }
  if (!fake_ollama_server_available) {
    qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  }

  const auto warm_style = style_by_id(QStringLiteral("warm-editorial-wedding"));
  if (warm_style.isEmpty()) {
    return fail("warm built-in style should exist");
  }

  photo_editor::desktop::AgentLocalModelPlanRequest request;
  request.model_id = QStringLiteral("rulebook-deterministic");
  request.style = warm_style;
  request.user_instructions = QStringLiteral("Keep it bright and soft but preserve highlights.");
  request.strategy_phase = QStringLiteral("sample");
  request.memory_packet = QVariantMap{
      {QStringLiteral("phase"), QStringLiteral("sample")},
      {QStringLiteral("includedMemoryKinds"), QStringList{QStringLiteral("style"), QStringLiteral("tool")}},
      {QStringLiteral("rawVaultIncluded"), false},
  };
  const auto plan = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (!plan.ok || plan.recipe_patch.isEmpty() || plan.plan_summary.isEmpty()) {
    return fail("deterministic local model should return a structured edit plan");
  }
  if (plan.prompt_audit.value(QStringLiteral("backend")).toString() != QStringLiteral("local") ||
      plan.prompt_audit.value(QStringLiteral("strategyPhase")).toString() != QStringLiteral("sample") ||
      !plan.prompt_audit.value(QStringLiteral("deterministicPlanning")).toBool() ||
      plan.prompt_audit.value(QStringLiteral("temperature")).toDouble() != 0.0 ||
      plan.prompt_audit.value(QStringLiteral("rawMemoryVaultIncluded")).toBool() ||
      !plan.prompt_audit.value(QStringLiteral("userPrompt")).toString().contains(QStringLiteral("warm-editorial-wedding")) ||
      !plan.prompt_audit.value(QStringLiteral("userPrompt")).toString().contains(QStringLiteral("Phase: sample")) ||
      plan.prompt_audit.value(QStringLiteral("userPromptCharCount")).toInt() <= 0) {
    return fail("deterministic local model should record phase-scoped deterministic prompt audit metadata");
  }
  if (plan.style_constraint_citations.isEmpty() ||
      !has_citation_field(
          plan.style_constraint_citations,
          QStringLiteral("constraints.max_exposure_shift")) ||
      !has_citation_field(
          plan.style_constraint_citations,
          QStringLiteral("constraints.max_temperature_shift")) ||
      !has_citation_field(
          plan.style_constraint_citations,
          QStringLiteral("constraints.color_constraints.max_hsl_channel_shift")) ||
      !has_citation_field(
          plan.style_constraint_citations,
          QStringLiteral("constraints.color_constraints.max_color_grading_saturation")) ||
      !has_citation_field(
          plan.style_constraint_citations,
          QStringLiteral("validation.highlight_clip_limit_percent"))) {
    return fail("local model plan should cite active style constraints and validation gates");
  }
  const auto color_patch = plan.recipe_patch.value(QStringLiteral("color")).toMap();
  const auto tone_patch = plan.recipe_patch.value(QStringLiteral("globalTone")).toMap();
  const auto color_mixer_patch = plan.recipe_patch.value(QStringLiteral("colorMixer")).toMap();
  const auto color_grading_patch = plan.recipe_patch.value(QStringLiteral("colorGrading")).toMap();
  if (color_patch.value(QStringLiteral("temperature")).toDouble() <= 0.0 ||
      tone_patch.value(QStringLiteral("highlights")).toDouble() >= 0.0) {
    return fail("warm style plan should warm color and protect highlights");
  }
  auto calibrated_request = request;
  calibrated_request.memory_packet.insert(
      QStringLiteral("includedMemoryKinds"),
      QStringList{QStringLiteral("style"), QStringLiteral("tool"),
                  QStringLiteral("photographer-calibration")});
  calibrated_request.memory_packet.insert(
      QStringLiteral("photographerCalibrationCard"),
      QVariantMap{
          {QStringLiteral("contract"),
           QStringLiteral("agent-photographer-calibration-card-v1")},
          {QStringLiteral("available"), true},
          {QStringLiteral("profileDigestSha256"),
           QString(64, QLatin1Char('a'))},
          {QStringLiteral("issueTagCounts"),
           QVariantMap{{QStringLiteral("too-warm"), 1}}},
          {QStringLiteral("validationConcernKeys"),
           QStringList{QStringLiteral("warmth")}},
          {QStringLiteral("planningGuidance"),
           QStringList{QStringLiteral(
               "Prioritize naturalness and avoid excess warmth.")}},
          {QStringLiteral("numericThresholdOverridesAllowed"), false},
      });
  const auto calibrated_plan =
      photo_editor::desktop::plan_agent_recipe_patch(calibrated_request);
  const auto calibrated_color =
      calibrated_plan.recipe_patch.value(QStringLiteral("color")).toMap();
  if (!calibrated_plan.ok ||
      calibrated_color.value(QStringLiteral("temperature")).toDouble() <= 0.0 ||
      calibrated_color.value(QStringLiteral("temperature")).toDouble() >=
          color_patch.value(QStringLiteral("temperature")).toDouble() ||
      !calibrated_plan.prompt_audit
           .value(QStringLiteral("photographerCalibrationAvailable"))
           .toBool() ||
      !calibrated_plan.prompt_audit
           .value(QStringLiteral("photographerCalibrationApplied"))
           .toBool() ||
      !calibrated_plan.prompt_audit
           .value(QStringLiteral("photographerCalibrationAdjustedGroups"))
           .toStringList()
           .contains(QStringLiteral("warmth")) ||
      calibrated_plan.prompt_audit
          .value(QStringLiteral(
              "photographerCalibrationNumericThresholdOverrides"))
          .toBool() ||
      calibrated_plan.warnings.isEmpty() ||
      !calibrated_plan.plan_summary.contains(
          QStringLiteral("blind photographer calibration"))) {
    return fail("deterministic planning should conservatively temper a matching response using project-local photographer calibration without changing thresholds");
  }
  auto north_star_request = request;
  north_star_request.memory_packet.insert(
      QStringLiteral("includedMemoryKinds"),
      QStringList{QStringLiteral("style"), QStringLiteral("tool"),
                  QStringLiteral("project-brief")});
  north_star_request.memory_packet.insert(
      QStringLiteral("projectNorthStar"),
      QVariantMap{
          {QStringLiteral("contract"),
           QStringLiteral("agent-project-north-star-memory-v1")},
          {QStringLiteral("available"), true},
          {QStringLiteral("projectKey"), QStringLiteral("Wedding 2026")},
          {QStringLiteral("brief"),
           QStringLiteral(
               "Keep the assignment warm, monochrome, and tightly cropped.")},
          {QStringLiteral("digestSha256"), QString(64, QLatin1Char('c'))},
          {QStringLiteral("authority"), QStringLiteral("qualitative-only")},
      });
  north_star_request.user_instructions = QStringLiteral(
      "Keep it in color, make it cool and natural, and preserve framing.");
  const auto north_star_plan =
      photo_editor::desktop::plan_agent_recipe_patch(north_star_request);
  if (!north_star_plan.ok ||
      north_star_plan.edit_intent.value(QStringLiteral("colorMode")).toString() !=
          QStringLiteral("color") ||
      north_star_plan.edit_intent.value(QStringLiteral("palette")).toString() !=
          QStringLiteral("cool") ||
      north_star_plan.edit_intent.value(QStringLiteral("crop")).toString() !=
          QStringLiteral("preserve") ||
      north_star_plan.recipe_patch
              .value(QStringLiteral("color"))
              .toMap()
              .value(QStringLiteral("temperature"))
              .toDouble() >= 0.0 ||
      !north_star_plan.prompt_audit
           .value(QStringLiteral("projectNorthStarAvailable"))
           .toBool() ||
      north_star_plan.prompt_audit
              .value(QStringLiteral("projectNorthStarDigest"))
              .toString() != QString(64, QLatin1Char('c')) ||
      !north_star_plan.prompt_audit
           .value(QStringLiteral("projectNorthStarCurrentRequestWins"))
           .toBool() ||
      !north_star_plan.prompt_audit
           .value(QStringLiteral("userPrompt"))
           .toString()
           .contains(QStringLiteral("Project North Star: Keep the assignment warm")) ||
      !north_star_plan.prompt_audit
           .value(QStringLiteral("userPrompt"))
           .toString()
           .contains(QStringLiteral("Current request: Keep it in color"))) {
    return fail("project North Star should shape deterministic planning while explicit current-request color, palette, and framing constraints win conflicts");
  }
  if (color_mixer_patch.value(QStringLiteral("hslChannel")).toString() != QStringLiteral("Oranges") ||
      color_mixer_patch.value(QStringLiteral("orangesSaturation")).toDouble() > 12.0 ||
      !color_mixer_patch.value(QStringLiteral("skinToneProtectionEnabled")).toBool() ||
      color_grading_patch.value(QStringLiteral("gradingMidtonesSaturation")).toDouble() > 12.0 ||
      !color_grading_patch.value(QStringLiteral("colorHarmonyEnabled")).toBool()) {
    return fail("warm style plan should honor color-specific HSL, grading, harmony, and protected-skin constraints");
  }
  const auto validation =
      photo_editor::desktop::validate_agent_recipe_patch(plan.recipe_patch, warm_style);
  if (!validation.valid) {
    std::cerr << validation.errors.join(QStringLiteral("; ")).toStdString() << '\n';
    return fail("local model plans must satisfy recipe patch validation");
  }

  auto constrained_style = warm_style;
  auto constraints = constrained_style.value(QStringLiteral("constraints")).toMap();
  constraints.insert(QStringLiteral("max_exposure_shift"), 0.1);
  constraints.insert(QStringLiteral("max_saturation_shift"), 4.0);
  constrained_style.insert(QStringLiteral("constraints"), constraints);
  request.style = constrained_style;
  request.user_instructions = QStringLiteral("Make it bright and colorful.");
  const auto constrained_plan = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (!constrained_plan.ok) {
    return fail("constrained style should still produce a plan");
  }
  const auto constrained_tone =
      constrained_plan.recipe_patch.value(QStringLiteral("globalTone")).toMap();
  const auto constrained_color =
      constrained_plan.recipe_patch.value(QStringLiteral("color")).toMap();
  if (constrained_tone.value(QStringLiteral("exposure")).toDouble() > 0.1 ||
      constrained_color.value(QStringLiteral("vibrance")).toDouble() > 4.0) {
    return fail("local model plan should honor style max shift constraints");
  }

  request.style = warm_style;
  request.user_instructions =
      QStringLiteral("Make it super contrasty black and white and give it a better crop.");
  const auto explicit_intent_plan =
      photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto explicit_color =
      explicit_intent_plan.recipe_patch.value(QStringLiteral("color")).toMap();
  const auto explicit_tone =
      explicit_intent_plan.recipe_patch.value(QStringLiteral("globalTone")).toMap();
  const auto explicit_crop =
      explicit_intent_plan.recipe_patch.value(QStringLiteral("crop")).toMap();
  if (!explicit_intent_plan.ok ||
      explicit_intent_plan.edit_intent.value(QStringLiteral("colorMode")).toString() !=
          QStringLiteral("monochrome") ||
      explicit_intent_plan.edit_intent.value(QStringLiteral("contrast")).toString() !=
          QStringLiteral("strong") ||
      explicit_intent_plan.edit_intent.value(QStringLiteral("crop")).toString() ==
          QStringLiteral("preserve") ||
      explicit_color.value(QStringLiteral("saturation")).toDouble() != -100.0 ||
      explicit_tone.value(QStringLiteral("contrast")).toDouble() < 35.0 ||
      explicit_crop.value(QStringLiteral("cropWidth")).toDouble() >= 0.98 ||
      explicit_crop.value(QStringLiteral("cropHeight")).toDouble() >= 0.98 ||
      explicit_intent_plan.recipe_patch.contains(QStringLiteral("colorGrading")) ||
      explicit_intent_plan.recipe_patch.contains(QStringLiteral("colorMixer"))) {
    return fail("explicit monochrome, strong-contrast, and crop instructions must survive style constraints");
  }

  request.user_instructions =
      QStringLiteral("Make it cooler, quieter, and more editorial.");
  const auto alternative_direction_plan =
      photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto alternative_color =
      alternative_direction_plan.recipe_patch.value(QStringLiteral("color")).toMap();
  const auto alternative_tone =
      alternative_direction_plan.recipe_patch.value(QStringLiteral("globalTone")).toMap();
  if (!alternative_direction_plan.ok ||
      alternative_direction_plan.edit_intent.value(QStringLiteral("palette")).toString() !=
          QStringLiteral("cool") ||
      alternative_direction_plan.edit_intent.value(QStringLiteral("contrast")).toString() !=
          QStringLiteral("soft") ||
      alternative_color.value(QStringLiteral("temperature")).toDouble() >= 0.0 ||
      alternative_color.value(QStringLiteral("saturation")).toDouble() >= 0.0 ||
      alternative_tone.value(QStringLiteral("contrast")).toDouble() >= 0.0) {
    return fail("explicit user direction should override conflicting warm style adjectives in the deterministic plan");
  }

  request.current_adjustment_patch_json = QStringLiteral(
      R"json({"format":"photo-editor-adjustment-patch","version":1,"scope":"asset-bound","adjustmentState":{"globalAdjustments":{"exposure":1.25,"contrast":10.0,"highlights":-18.0,"shadows":44.0,"whites":8.0,"blacks":14.0,"temperature":5.0,"tint":1.0,"vibrance":18.0,"saturation":4.0}}})json");
  request.user_instructions =
      QStringLiteral("Make this image brighter overall while preserving natural color.");
  const auto relative_brightness_plan =
      photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto relative_brightness_tone =
      relative_brightness_plan.recipe_patch.value(QStringLiteral("globalTone")).toMap();
  if (!relative_brightness_plan.ok ||
      relative_brightness_tone.value(QStringLiteral("exposure")).toDouble() <= 1.25 ||
      relative_brightness_tone.value(QStringLiteral("contrast")).toDouble() != 10.0) {
    return fail("directional brightness edits should move from the current recipe instead of replacing it with a neutral-style absolute value");
  }

  request.user_instructions =
      QStringLiteral("Make the foliage more vibrant with richer color.");
  const auto relative_saturation_plan =
      photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto relative_saturation_color =
      relative_saturation_plan.recipe_patch.value(QStringLiteral("color")).toMap();
  if (!relative_saturation_plan.ok ||
      relative_saturation_plan.edit_intent.value(QStringLiteral("saturation")).toString() !=
          QStringLiteral("more") ||
      relative_saturation_color.value(QStringLiteral("vibrance")).toDouble() <= 18.0 ||
      relative_saturation_color.value(QStringLiteral("saturation")).toDouble() <= 4.0 ||
      relative_saturation_plan.recipe_patch.contains(QStringLiteral("colorGrading")) ||
      relative_saturation_plan.recipe_patch.contains(QStringLiteral("colorMixer"))) {
    return fail("explicit richer-color edits should increase current color intensity without adding unrelated style color transforms");
  }

  request.user_instructions =
      QStringLiteral("Make this portrait warmer overall while keeping the tone unchanged.");
  const auto preserve_tone_warm_plan =
      photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto preserve_tone_warm_global =
      preserve_tone_warm_plan.recipe_patch.value(QStringLiteral("globalTone")).toMap();
  const auto preserve_tone_warm_color =
      preserve_tone_warm_plan.recipe_patch.value(QStringLiteral("color")).toMap();
  if (!preserve_tone_warm_plan.ok ||
      preserve_tone_warm_color.value(QStringLiteral("temperature")).toDouble() <= 5.0 ||
      preserve_tone_warm_global.value(QStringLiteral("exposure")).toDouble() != 1.25 ||
      preserve_tone_warm_global.value(QStringLiteral("contrast")).toDouble() != 10.0) {
    return fail("explicit warmer edits with tone preservation should change current white balance without rewriting tone");
  }
  request.current_adjustment_patch_json.clear();

  auto reference_style = warm_style;
  auto reference_constraints = reference_style.value(QStringLiteral("constraints")).toMap();
  auto reference_color_constraints =
      reference_constraints.value(QStringLiteral("color_constraints")).toMap();
  reference_color_constraints.insert(
      QStringLiteral("preferred_temperature_bias"), QStringLiteral("neutral"));
  reference_constraints.insert(QStringLiteral("color_constraints"), reference_color_constraints);
  reference_style.insert(QStringLiteral("constraints"), reference_constraints);
  reference_style.insert(
      QStringLiteral("learnedProfile"),
      QVariantMap{
          {QStringLiteral("source_asset_count"), 1},
          {QStringLiteral("editTendencies"),
           QVariantMap{
               {QStringLiteral("averageExposure"), -0.53},
               {QStringLiteral("averageContrast"), -89.0},
               {QStringLiteral("averageHighlights"), 81.0},
               {QStringLiteral("averageShadows"), 24.0},
               {QStringLiteral("averageTemperature"), -23.9},
               {QStringLiteral("averageTint"), 16.8},
               {QStringLiteral("averageVibrance"), 94.0},
               {QStringLiteral("averageSaturation"), -27.0},
           }},
      });
  request.style = reference_style;
  request.user_instructions = QStringLiteral("Follow the selected inspiration and make the color funky.");
  const auto reference_plan = photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto reference_tone = reference_plan.recipe_patch.value(QStringLiteral("globalTone")).toMap();
  const auto reference_color = reference_plan.recipe_patch.value(QStringLiteral("color")).toMap();
  if (!reference_plan.ok ||
      !reference_plan.prompt_audit.value(QStringLiteral("learnedReferenceApplied")).toBool() ||
      reference_plan.prompt_audit.value(QStringLiteral("learnedReferenceAssetCount")).toInt() != 1 ||
      std::abs(reference_tone.value(QStringLiteral("exposure")).toDouble() - (-0.53)) > 0.001 ||
      std::abs(reference_tone.value(QStringLiteral("contrast")).toDouble() - (-89.0)) > 0.001 ||
      std::abs(reference_color.value(QStringLiteral("temperature")).toDouble() - (-23.9)) > 0.001 ||
      std::abs(reference_color.value(QStringLiteral("vibrance")).toDouble() - 94.0) > 0.001 ||
      std::abs(reference_color.value(QStringLiteral("saturation")).toDouble() - (-27.0)) > 0.001) {
    return fail("deterministic fallback should apply the selected reference profile instead of a generic recipe");
  }

  const auto fast_highlight_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/fast-highlight.png"));
  if (!QDir().mkpath(QFileInfo(fast_highlight_path).dir().absolutePath())) {
    return fail("fast local vision fixture directory should be created");
  }
  QImage fast_highlight_fixture(800, 600, QImage::Format_RGB32);
  fast_highlight_fixture.fill(QColor(66, 74, 76));
  for (auto y = 40; y < 250; ++y) {
    for (auto x = 575; x < 770; ++x) {
      fast_highlight_fixture.setPixelColor(x, y, QColor(254, 248, 225));
    }
  }
  if (!fast_highlight_fixture.save(fast_highlight_path)) {
    return fail("fast local vision fixture should be writable");
  }
  photo_editor::desktop::AgentLocalModelPlanRequest fast_highlight_request;
  fast_highlight_request.model_id = QStringLiteral("ollama-qwen-test");
  fast_highlight_request.style = warm_style;
  fast_highlight_request.asset = QVariantMap{
      {QStringLiteral("assetId"), QStringLiteral("fast-highlight")},
      {QStringLiteral("filePath"), fast_highlight_path},
  };
  fast_highlight_request.current_adjustment_patch_json =
      QStringLiteral("{\"kind\":\"photo-editor-adjustment-patch\",\"version\":1,\"scope\":\"asset-bound\",\"groups\":{}}");
  fast_highlight_request.user_instructions = QStringLiteral(
      "Keep realistic color and framing. Subdue only the bright upper-right window so the rest remains unchanged.");
  QElapsedTimer fast_highlight_timer;
  fast_highlight_timer.start();
  const auto fast_highlight_plan =
      photo_editor::desktop::plan_agent_recipe_patch(fast_highlight_request);
  const auto fast_highlight_elapsed_ms = fast_highlight_timer.elapsed();
  const auto fast_highlight_actions =
      fast_highlight_plan.edit_intent.value(QStringLiteral("localizedActions")).toList();
  const auto fast_highlight_action =
      fast_highlight_actions.isEmpty() ? QVariantMap{} : fast_highlight_actions.first().toMap();
  const auto fast_highlight_findings =
      fast_highlight_plan.edit_intent.value(QStringLiteral("visualFindings")).toList();
  const auto fast_highlight_finding =
      fast_highlight_findings.isEmpty() ? QVariantMap{} : fast_highlight_findings.first().toMap();
  const auto fast_highlight_masks = fast_highlight_plan.recipe_patch
                                        .value(QStringLiteral("masks"))
                                        .toMap()
                                        .value(QStringLiteral("appendMaskStack"))
                                        .toList();
  const auto fast_highlight_mask =
      fast_highlight_masks.isEmpty() ? QVariantMap{} : fast_highlight_masks.first().toMap();
  const auto fast_highlight_region =
      fast_highlight_action.value(QStringLiteral("region")).toMap();
  const auto fast_highlight_mask_id =
      fast_highlight_action.value(QStringLiteral("maskId")).toString();
  const auto fast_highlight_replay =
      photo_editor::desktop::plan_agent_recipe_patch(fast_highlight_request);
  const auto fast_highlight_replay_actions =
      fast_highlight_replay.edit_intent.value(QStringLiteral("localizedActions")).toList();
  const auto fast_highlight_replay_mask_id =
      fast_highlight_replay_actions.isEmpty()
          ? QString{}
          : fast_highlight_replay_actions.first()
                .toMap()
                .value(QStringLiteral("maskId"))
                .toString();
  if (!fast_highlight_plan.ok ||
      fast_highlight_plan.model_id != QStringLiteral("fast-local-vision") ||
      fast_highlight_plan.prompt_audit.value(QStringLiteral("plannerMode")).toString() !=
          QStringLiteral("fast-local-highlight") ||
      fast_highlight_plan.prompt_audit.value(QStringLiteral("modelInvoked")).toBool() ||
      fast_highlight_plan.edit_intent.value(QStringLiteral("globalChangePolicy")).toString() !=
          QStringLiteral("preserve") ||
      fast_highlight_actions.size() != 1 ||
      fast_highlight_findings.size() != 1 ||
      fast_highlight_mask_id.isEmpty() ||
      fast_highlight_finding.value(QStringLiteral("maskId")).toString() !=
          fast_highlight_mask_id ||
      fast_highlight_mask.value(QStringLiteral("id")).toString() !=
          fast_highlight_mask_id ||
      fast_highlight_replay_mask_id != fast_highlight_mask_id ||
      fast_highlight_region.value(QStringLiteral("centerX")).toDouble() < 0.65 ||
      fast_highlight_region.value(QStringLiteral("centerY")).toDouble() > 0.45 ||
      fast_highlight_plan.recipe_patch.value(QStringLiteral("masks"))
          .toMap()
          .value(QStringLiteral("appendMaskStack"))
          .toList()
          .size() != 1 ||
      fast_highlight_plan.recipe_patch.contains(QStringLiteral("globalTone")) ||
      fast_highlight_elapsed_ms >= 1000) {
    return fail("explicit localized highlight requests should use an immediate grounded planner with linked finding, action, and mask identities");
  }

  const auto fake_image_path = QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/source-file.raw"));
  if (!QDir().mkpath(QFileInfo(fake_image_path).dir().absolutePath())) {
    return fail("fake proof fixture directory should be created");
  }
  QFile fake_image_file(fake_image_path);
  if (!fake_image_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return fail("fake proof fixture should be writable");
  }
  fake_image_file.write("fake source bytes");
  fake_image_file.close();
  const auto fake_proof_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/fake-rendered-proof.png"));
  QFile fake_proof_file(fake_proof_path);
  if (!fake_proof_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return fail("fake rendered proof fixture should be writable");
  }
  const QByteArray fake_proof_bytes("fake rendered proof bytes");
  fake_proof_file.write(fake_proof_bytes);
  fake_proof_file.close();
  const QJsonObject model_plan{
      {QStringLiteral("decision"), QStringLiteral("edit_intent")},
      {QStringLiteral("summary"), QStringLiteral("Fake Ollama warm proof edit")},
      {QStringLiteral("styleConstraintCitations"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("field"), QStringLiteral("constraints.max_exposure_shift")},
               {QStringLiteral("reason"), QStringLiteral("Limits exposure changes.")},
           },
           QJsonObject{
               {QStringLiteral("field"), QStringLiteral("allowedTools.color")},
               {QStringLiteral("reason"), QStringLiteral("Color edits are allowed.")},
           },
       }},
      {QStringLiteral("editIntent"),
       QJsonObject{
           {QStringLiteral("goalSummary"), QStringLiteral("Create a polished warm reference-led edit.")},
           {QStringLiteral("colorMode"), QStringLiteral("color")},
           {QStringLiteral("contrast"), QStringLiteral("natural")},
           {QStringLiteral("brightness"), QStringLiteral("preserve")},
           {QStringLiteral("palette"), QStringLiteral("warm")},
           {QStringLiteral("crop"), QStringLiteral("preserve")},
           {QStringLiteral("detail"), QStringLiteral("natural")},
           {QStringLiteral("mood"), QStringLiteral("editorial")},
           {QStringLiteral("referenceStrength"), 1.0},
           {QStringLiteral("requestedActions"), QJsonArray{QStringLiteral("tone"), QStringLiteral("color")}},
           {QStringLiteral("requiresApproval"), QJsonArray{}},
           {QStringLiteral("confidence"), 0.92},
           {QStringLiteral("globalChangePolicy"), QStringLiteral("allow")},
           {QStringLiteral("visualFindings"),
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("kind"), QStringLiteral("subject_exposure")},
                    {QStringLiteral("target"), QStringLiteral("subject")},
                    {QStringLiteral("severity"), QStringLiteral("medium")},
                    {QStringLiteral("observation"),
                     QStringLiteral("The central couple can take a restrained local lift.")},
                    {QStringLiteral("confidence"), 0.88},
                    {QStringLiteral("region"),
                     QJsonObject{
                         {QStringLiteral("centerX"), 0.50},
                         {QStringLiteral("centerY"), 0.52},
                         {QStringLiteral("radiusX"), 0.24},
                         {QStringLiteral("radiusY"), 0.34},
                     }},
                },
            }},
           {QStringLiteral("localizedActions"),
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("target"), QStringLiteral("subject")},
                    {QStringLiteral("maskShape"), QStringLiteral("radial")},
                    {QStringLiteral("selectionMode"), QStringLiteral("semantic")},
                    {QStringLiteral("reason"),
                     QStringLiteral("Lift the complete visible subject without changing the background.")},
                    {QStringLiteral("confidence"), 0.86},
                    {QStringLiteral("region"),
                     QJsonObject{
                         {QStringLiteral("centerX"), 0.50},
                         {QStringLiteral("centerY"), 0.52},
                         {QStringLiteral("radiusX"), 0.24},
                         {QStringLiteral("radiusY"), 0.34},
                         {QStringLiteral("feather"), 0.72},
                     }},
                    {QStringLiteral("adjustments"),
                     QJsonObject{
                         {QStringLiteral("exposure"), 0.24},
                         {QStringLiteral("shadows"), 12.0},
                     }},
                },
            }},
      }},
      {QStringLiteral("warnings"), QJsonArray{QStringLiteral("fake-server-warning")}},
  };
  const QJsonObject fake_ollama_response{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"),
       QJsonObject{
           {QStringLiteral("role"), QStringLiteral("assistant")},
           {QStringLiteral("content"), QString()},
           {QStringLiteral("thinking"), QString::fromUtf8(QJsonDocument(model_plan).toJson(QJsonDocument::Compact))},
       }},
  };
  const auto fake_ollama_request_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("ollama-request.json"));
  if (fake_ollama_server_available) {
    fake_ollama_server.setChunkedResponse(QStringLiteral("/api/chat"), fake_ollama_response);
  } else {
    qputenv(
        "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
        QJsonDocument(fake_ollama_response).toJson(QJsonDocument::Compact));
    qputenv("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH", fake_ollama_request_path.toUtf8());
  }
  request.model_id = QStringLiteral("ollama-qwen-test");
  auto learned_warm_style = warm_style;
  learned_warm_style.insert(
      QStringLiteral("learnedProfile"),
      QVariantMap{
          {QStringLiteral("source_asset_count"), 3},
          {QStringLiteral("usable_recipe_count"), 3},
          {QStringLiteral("styleSummary"), QStringLiteral("Learned from Brady's warm edited wedding references.")},
          {QStringLiteral("editTendencies"),
           QVariantMap{
               {QStringLiteral("averageExposure"), 0.18},
               {QStringLiteral("averageTemperature"), 240.0},
           }},
      });
  learned_warm_style.insert(
      QStringLiteral("toolKnowledge"),
      QVariantMap{{QStringLiteral("hsl"), QStringLiteral("Treat orange/red channels as skin-sensitive.")}});
  learned_warm_style.insert(
      QStringLiteral("qualityModel"),
      QVariantMap{{QStringLiteral("priorities"), QStringList{QStringLiteral("preserve highlight detail")}}});
  learned_warm_style.insert(
      QStringLiteral("projectWorkflow"),
      QVariantMap{{QStringLiteral("editRepresentativeSamplesFirst"), true}});
  request.style = learned_warm_style;
  request.asset = QVariantMap{
      {QStringLiteral("filePath"), fake_image_path},
      {QStringLiteral("proofImagePath"), fake_proof_path},
  };
  request.current_adjustment_patch_json =
      QStringLiteral("{\"kind\":\"photo-editor-adjustment-patch\",\"version\":1,\"scope\":\"asset-bound\",\"groups\":{}}");
  request.user_instructions = QStringLiteral("Use the image and make a polished warm edit.");
  request.strategy_phase = QStringLiteral("batch");
  request.memory_packet = QVariantMap{
      {QStringLiteral("phase"), QStringLiteral("batch")},
      {QStringLiteral("includedMemoryKinds"),
       QStringList{
           QStringLiteral("approved-sample-recipe"),
           QStringLiteral("cluster-summary"),
           QStringLiteral("style-constraints"),
           QStringLiteral("photographer-calibration"),
      }},
      {QStringLiteral("rawVaultIncluded"), false},
      {QStringLiteral("projectNorthStar"),
       QVariantMap{
           {QStringLiteral("contract"),
            QStringLiteral("agent-project-north-star-memory-v1")},
           {QStringLiteral("available"), true},
           {QStringLiteral("projectKey"), QStringLiteral("Wedding 2026")},
           {QStringLiteral("brief"),
            QStringLiteral(
                "Keep this wedding intimate and documentary; protect skin, highlights, and unposed moments across every delivery.")},
           {QStringLiteral("digestSha256"), QString(64, QLatin1Char('c'))},
           {QStringLiteral("authority"), QStringLiteral("qualitative-only")},
       }},
      {QStringLiteral("approvedSampleRecipeCard"),
       QVariantMap{
           {QStringLiteral("available"), true},
           {QStringLiteral("validationOk"), true},
           {QStringLiteral("runId"), QStringLiteral("approved-run-1")},
           {QStringLiteral("scope"), QStringLiteral("selected-assets")},
           {QStringLiteral("recipePatch"),
            QVariantMap{
                {QStringLiteral("globalTone"),
                 QVariantMap{{QStringLiteral("exposure"), 0.12}}},
            }},
       }},
      {QStringLiteral("approvedProjectTasteCard"),
       QVariantMap{
           {QStringLiteral("available"), true},
           {QStringLiteral("projectKey"), QStringLiteral("Wedding 2026")},
           {QStringLiteral("twoSpeedPreference"),
            QVariantMap{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-two-speed-taste-signal-v1")},
                {QStringLiteral("available"), true},
                {QStringLiteral("choiceCount"), 3},
                {QStringLiteral("firstLookCount"), 2},
                {QStringLiteral("deepReviewCount"), 1},
                {QStringLiteral("minimumEvidenceCount"), 2},
                {QStringLiteral("enoughEvidence"), true},
                {QStringLiteral("preferredVersion"),
                 QStringLiteral("first-look")},
                {QStringLiteral("influencesPlanning"), true},
                {QStringLiteral("planningGuidance"),
                 QStringLiteral(
                     "Prefer restrained, direct edits close to the proved first look unless this photo or request clearly benefits from deeper refinement.")},
                {QStringLiteral("authority"),
                 QStringLiteral("project-local-qualitative-guidance-only")},
                {QStringLiteral("currentRequestWins"), true},
                {QStringLiteral("automaticVersionSelectionAllowed"), false},
                {QStringLiteral("automaticCommitAllowed"), false},
            }},
           {QStringLiteral("corrections"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("feedback"),
                     QStringLiteral("Keep skin less warm and leave the crop looser.")},
                    {QStringLiteral("revisedFromRunId"),
                     QStringLiteral("rejected-project-run-1")},
                    {QStringLiteral("approvedRunId"),
                     QStringLiteral("approved-correction-run-1")},
                    {QStringLiteral("rejectedRecipeExamples"), QVariantList{}},
                    {QStringLiteral("approvedRecipeExamples"), QVariantList{}},
                },
            }},
           {QStringLiteral("rejectedDrafts"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("runId"),
                     QStringLiteral("discarded-project-run-1")},
                    {QStringLiteral("recipeExamples"), QVariantList{}},
                    {QStringLiteral("inferredReason"), false},
                },
            }},
           {QStringLiteral("exemplars"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("runId"), QStringLiteral("approved-project-run-1")},
                    {QStringLiteral("userIntent"), QStringLiteral("Keep skin warm and highlights restrained.")},
                    {QStringLiteral("assetExamples"),
                     QVariantList{
                         QVariantMap{
                             {QStringLiteral("assetId"), QStringLiteral("approved-asset-1")},
                             {QStringLiteral("recipeDiff"),
                              QVariantMap{
                                  {QStringLiteral("globalTone"),
                                   QVariantMap{
                                       {QStringLiteral("exposure"),
                                        QVariantMap{
                                            {QStringLiteral("before"), 0.0},
                                            {QStringLiteral("after"), 0.12},
                                            {QStringLiteral("delta"), 0.12},
                                        }},
                                   }},
                              }},
                         },
                     }},
                },
           }},
       }},
      {QStringLiteral("photographerCalibrationCard"),
       QVariantMap{
           {QStringLiteral("available"), true},
           {QStringLiteral("profileDigestSha256"),
            QString(64, QLatin1Char('b'))},
           {QStringLiteral("weakestDimension"),
            QStringLiteral("naturalness")},
           {QStringLiteral("validationConcernKeys"),
            QStringList{QStringLiteral("warmth")}},
           {QStringLiteral("planningGuidance"),
            QStringList{QStringLiteral(
                "Prioritize naturalness and use restrained warmth.")}},
           {QStringLiteral("numericThresholdOverridesAllowed"), false},
           {QStringLiteral("professionalReadinessClaimed"), false},
       }},
  };
  const auto ollama_plan = photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto fake_request = fake_ollama_server_available
                                ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
                                : read_json_file(fake_ollama_request_path);
  const auto fake_messages = fake_request.value(QStringLiteral("messages")).toArray();
  const auto fake_format = fake_request.value(QStringLiteral("format")).toObject();
  const auto fake_required_fields = fake_format.value(QStringLiteral("required")).toArray();
  const auto fake_edit_intent_schema =
      fake_format.value(QStringLiteral("properties"))
          .toObject()
          .value(QStringLiteral("editIntent"))
          .toObject();
  const auto fake_edit_intent_required =
      fake_edit_intent_schema.value(QStringLiteral("required")).toArray();
  const auto fake_decision_values = fake_format.value(QStringLiteral("properties"))
                                        .toObject()
                                        .value(QStringLiteral("decision"))
                                        .toObject()
                                        .value(QStringLiteral("enum"))
                                        .toArray();
  const auto fake_system_prompt =
      fake_messages.isEmpty()
          ? QString{}
          : fake_messages.first().toObject().value(QStringLiteral("content")).toString();
  const auto fake_user_message = fake_messages.size() >= 2 ? fake_messages.at(1).toObject() : QJsonObject{};
  const auto fake_user_prompt = fake_user_message.value(QStringLiteral("content")).toString();
  const auto fake_ollama_request_ok =
      fake_request.value(QStringLiteral("model")).toString() == QStringLiteral("qwen2.5vl:7b") &&
      !fake_request.value(QStringLiteral("stream")).toBool(true) &&
      !fake_request.value(QStringLiteral("think")).toBool(true) &&
      fake_request.value(QStringLiteral("keep_alive")).toString() == QStringLiteral("10m") &&
      fake_request.value(QStringLiteral("options")).toObject().value(QStringLiteral("temperature")).toDouble(-1.0) == 0.0 &&
      fake_request.value(QStringLiteral("options")).toObject().value(QStringLiteral("num_predict")).toInt() == 640 &&
      fake_request.value(QStringLiteral("options")).toObject().value(QStringLiteral("num_ctx")).toInt() >= 12288 &&
      fake_request.value(QStringLiteral("options")).toObject().value(QStringLiteral("num_ctx")).toInt() <= 24576 &&
      fake_request.value(QStringLiteral("format")).isObject() &&
      fake_required_fields.contains(QStringLiteral("styleConstraintCitations")) &&
      fake_required_fields.contains(QStringLiteral("editIntent")) &&
      fake_edit_intent_required.contains(QStringLiteral("visualFindings")) &&
      fake_edit_intent_required.contains(QStringLiteral("localizedActions")) &&
      fake_edit_intent_required.contains(QStringLiteral("globalChangePolicy")) &&
      !fake_edit_intent_required.contains(QStringLiteral("colorMode")) &&
      !fake_edit_intent_required.contains(QStringLiteral("crop")) &&
      fake_decision_values.contains(QStringLiteral("edit_intent")) &&
      !fake_system_prompt.contains(QStringLiteral("compact wire format")) &&
      fake_user_prompt.contains(QStringLiteral("Strategy phase: batch")) &&
      fake_user_prompt.contains(QStringLiteral("Phase: batch")) &&
      fake_user_prompt.contains(QStringLiteral("Retrieved memory packet JSON")) &&
      fake_user_prompt.contains(QStringLiteral("approved-run-1")) &&
      fake_user_prompt.contains(QStringLiteral("projectNorthStar")) &&
      fake_user_prompt.contains(QStringLiteral("Keep this wedding intimate and documentary")) &&
      fake_user_prompt.contains(QStringLiteral("the current request wins every conflict")) &&
      fake_user_prompt.contains(QStringLiteral("Never infer numeric recipe changes, culling, retouch, or destructive authority")) &&
      fake_user_prompt.contains(QStringLiteral("Compact relevant examples JSON")) &&
      fake_user_prompt.contains(QStringLiteral("approved-sample-recipe")) &&
      fake_user_prompt.contains(QStringLiteral("approved-project-edit")) &&
      fake_user_prompt.contains(QStringLiteral("approved-project-correction")) &&
      fake_user_prompt.contains(QStringLiteral("rejected-project-edit")) &&
      fake_user_prompt.contains(QStringLiteral("Keep skin less warm")) &&
      fake_user_prompt.contains(QStringLiteral("never invent a rejection reason")) &&
      fake_user_prompt.contains(QStringLiteral("approved-project-run-1")) &&
      fake_user_prompt.contains(QStringLiteral("Wedding 2026")) &&
      fake_user_prompt.contains(QStringLiteral("preference evidence only for the active project")) &&
      fake_user_prompt.contains(QStringLiteral("twoSpeedPreference")) &&
      fake_user_prompt.contains(QStringLiteral("agent-two-speed-taste-signal-v1")) &&
      fake_user_prompt.contains(QStringLiteral("Prefer restrained, direct edits close to the proved first look")) &&
      fake_user_prompt.contains(QStringLiteral("current request, visible photo, active style, and safety policy always win")) &&
      fake_user_prompt.contains(QStringLiteral("Never select First Look or Deep Review automatically")) &&
      fake_user_prompt.contains(QStringLiteral("never commit from this preference")) &&
      fake_user_prompt.contains(QStringLiteral("Preserving framing or protecting highlights does not forbid")) &&
      fake_user_prompt.contains(QStringLiteral("photographerCalibrationCard")) &&
      fake_user_prompt.contains(QStringLiteral("Prioritize naturalness and use restrained warmth")) &&
      fake_user_prompt.contains(QStringLiteral("qualitative guidance only")) &&
      fake_user_prompt.contains(QStringLiteral("never claim professional readiness")) &&
      fake_user_prompt.contains(QStringLiteral("semantic editIntent")) &&
      fake_user_prompt.contains(QStringLiteral("Ground visualFindings in visible evidence")) &&
      fake_user_prompt.contains(QStringLiteral("bounded local change")) &&
      fake_user_prompt.contains(QStringLiteral("globalChangePolicy=preserve")) &&
      fake_user_prompt.contains(QStringLiteral("target=object")) &&
      fake_user_prompt.contains(QStringLiteral("targetLabel")) &&
      fake_user_prompt.contains(QStringLiteral("selectionMode=guided")) &&
      fake_user_prompt.contains(QStringLiteral("selectionCandidates")) &&
      fake_user_prompt.contains(QStringLiteral("do not guess silently")) &&
      fake_user_prompt.contains(QStringLiteral("recentVisualReferents")) &&
      fake_user_prompt.contains(QStringLiteral("exact active asset")) &&
      fake_user_prompt.contains(QStringLiteral("never resolve a vague pronoun by guessing")) &&
      fake_user_prompt.contains(QStringLiteral("Treat semantic and guided selection as requests")) &&
      fake_user_prompt.contains(QStringLiteral("learnedProfile")) &&
      fake_user_prompt.contains(QStringLiteral("toolKnowledge")) &&
      fake_user_prompt.contains(QStringLiteral("qualityModel")) &&
      fake_user_prompt.contains(QStringLiteral("projectWorkflow"));
  const auto fake_ollama_saw_image =
      !fake_user_message.value(QStringLiteral("images")).toArray().isEmpty();
  const auto fake_ollama_image_payload =
      fake_user_message.value(QStringLiteral("images")).toArray().isEmpty()
          ? QString()
          : fake_user_message.value(QStringLiteral("images")).toArray().first().toString();
  const auto ollama_findings =
      ollama_plan.edit_intent.value(QStringLiteral("visualFindings")).toList();
  const auto ollama_actions =
      ollama_plan.edit_intent.value(QStringLiteral("localizedActions")).toList();
  const auto ollama_masks = ollama_plan.recipe_patch
                                .value(QStringLiteral("masks"))
                                .toMap()
                                .value(QStringLiteral("appendMaskStack"))
                                .toList();
  const auto ollama_finding =
      ollama_findings.isEmpty() ? QVariantMap{} : ollama_findings.first().toMap();
  const auto ollama_action =
      ollama_actions.isEmpty() ? QVariantMap{} : ollama_actions.first().toMap();
  const auto ollama_mask =
      ollama_masks.isEmpty() ? QVariantMap{} : ollama_masks.first().toMap();
  const auto ollama_mask_id =
      ollama_action.value(QStringLiteral("maskId")).toString();
  if (!ollama_plan.ok ||
      ollama_plan.decision != QStringLiteral("recipe_patch") ||
      ollama_plan.plan_summary != QStringLiteral("Fake Ollama warm proof edit") ||
      ollama_plan.prompt_audit.value(QStringLiteral("backend")).toString() != QStringLiteral("ollama") ||
      ollama_plan.prompt_audit.value(QStringLiteral("strategyPhase")).toString() != QStringLiteral("batch") ||
      !ollama_plan.prompt_audit.value(QStringLiteral("deterministicPlanning")).toBool() ||
      ollama_plan.prompt_audit.value(QStringLiteral("temperature")).toDouble() != 0.0 ||
      ollama_plan.prompt_audit.value(QStringLiteral("memoryPacketPhase")).toString() != QStringLiteral("batch") ||
      ollama_plan.prompt_audit.value(QStringLiteral("fewShotExampleCount")).toInt() != 4 ||
      !ollama_plan.prompt_audit
           .value(QStringLiteral("photographerCalibrationAvailable"))
           .toBool() ||
      ollama_plan.prompt_audit
              .value(QStringLiteral("photographerCalibrationDigest"))
              .toString() != QString(64, QLatin1Char('b')) ||
      !ollama_plan.prompt_audit
           .value(QStringLiteral("projectNorthStarAvailable"))
           .toBool() ||
      ollama_plan.prompt_audit
              .value(QStringLiteral("projectNorthStarDigest"))
              .toString() != QString(64, QLatin1Char('c')) ||
      !ollama_plan.prompt_audit
           .value(QStringLiteral("projectNorthStarCurrentRequestWins"))
           .toBool() ||
      ollama_plan.prompt_audit.value(QStringLiteral("rawMemoryVaultIncluded")).toBool() ||
      !ollama_plan.prompt_audit.value(QStringLiteral("userPrompt")).toString().contains(QStringLiteral("semantic editIntent")) ||
      !ollama_plan.prompt_audit.value(QStringLiteral("imageIncluded")).toBool() ||
      ollama_plan.prompt_audit.value(QStringLiteral("imageByteCount")).toInt() != fake_proof_bytes.size() ||
      ollama_plan.prompt_audit.value(QStringLiteral("imageSourceKind")).toString() != QStringLiteral("proof") ||
      ollama_plan.prompt_audit.value(QStringLiteral("imageSourcePath")).toString() != fake_proof_path ||
      !ollama_plan.prompt_audit.value(QStringLiteral("transport")).toMap().value(QStringLiteral("ok")).toBool() ||
      ollama_plan.prompt_audit.value(QStringLiteral("ollamaResponseSource")).toString() != QStringLiteral("message.thinking") ||
      ollama_plan.prompt_audit.value(QStringLiteral("generationSchemaContract")).toString() !=
          QStringLiteral("agent-compact-semantic-intent-generation-v1") ||
      ollama_plan.prompt_audit.value(QStringLiteral("generationSchemaByteCount")).toInt() >= 2048 ||
      !ollama_plan.prompt_audit.value(QStringLiteral("responseHasEditIntent")).toBool() ||
      ollama_plan.prompt_audit.value(QStringLiteral("modelDecision")).toString() != QStringLiteral("edit_intent") ||
      ollama_plan.prompt_audit.value(QStringLiteral("intentCompiler")).toString() != QStringLiteral("rulebook-deterministic-v2") ||
      ollama_plan.prompt_audit.value(QStringLiteral("visualFindingCount")).toInt() != 1 ||
      ollama_plan.prompt_audit.value(QStringLiteral("localizedActionCount")).toInt() != 1 ||
      ollama_plan.prompt_audit.value(QStringLiteral("localizedMaskCount")).toInt() != 1 ||
      ollama_plan.prompt_audit.value(QStringLiteral("localizedMaskWriteMode")).toString() !=
          QStringLiteral("append-preserve-existing") ||
      ollama_plan.prompt_audit.value(QStringLiteral("globalChangePolicy")).toString() !=
          QStringLiteral("allow") ||
      ollama_plan.prompt_audit.value(QStringLiteral("responseCitationCount")).toInt() != 2 ||
      ollama_plan.edit_intent.value(QStringLiteral("palette")).toString() != QStringLiteral("warm") ||
      ollama_plan.edit_intent.value(QStringLiteral("mood")).toString() != QStringLiteral("editorial") ||
      ollama_plan.edit_intent.value(QStringLiteral("visualFindings")).toList().size() != 1 ||
      ollama_plan.edit_intent.value(QStringLiteral("localizedActions")).toList().size() != 1 ||
      ollama_mask_id.isEmpty() ||
      ollama_finding.value(QStringLiteral("maskId")).toString() != ollama_mask_id ||
      ollama_mask.value(QStringLiteral("id")).toString() != ollama_mask_id ||
      ollama_action.value(QStringLiteral("selectionMode")).toString() !=
          QStringLiteral("semantic") ||
      ollama_mask.value(QStringLiteral("agentSelectionMode")).toString() !=
          QStringLiteral("semantic") ||
      ollama_mask.value(QStringLiteral("agentSemanticTarget")).toString() !=
          QStringLiteral("subject") ||
      ollama_plan.recipe_patch.value(QStringLiteral("color")).toMap().value(QStringLiteral("temperature")).toDouble() <= 0.0 ||
      ollama_plan.recipe_patch.value(QStringLiteral("masks"))
              .toMap()
              .value(QStringLiteral("appendMaskStack"))
              .toList()
              .size() != 1 ||
      ollama_plan.style_constraint_citations.size() != 2 ||
      !ollama_plan.warnings.contains(QStringLiteral("fake-server-warning")) ||
      !fake_ollama_request_ok ||
      !fake_ollama_saw_image ||
      fake_ollama_image_payload != QString::fromLatin1(fake_proof_bytes.toBase64())) {
    return fail("Ollama backend should send rendered proof/schema context and recover structured JSON from Qwen thinking responses");
  }
  const auto ollama_validation =
      photo_editor::desktop::validate_agent_recipe_patch(ollama_plan.recipe_patch, warm_style);
  if (!ollama_validation.valid) {
    std::cerr << ollama_validation.errors.join(QStringLiteral("; ")).toStdString() << '\n';
    return fail("Ollama model recipe patches should satisfy recipe patch validation");
  }

  auto scope_misread_plan = model_plan;
  auto scope_misread_intent =
      scope_misread_plan.value(QStringLiteral("editIntent")).toObject();
  scope_misread_intent.insert(
      QStringLiteral("globalChangePolicy"), QStringLiteral("preserve"));
  scope_misread_plan.insert(
      QStringLiteral("editIntent"), scope_misread_intent);
  const QJsonObject scope_misread_response{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"),
       QJsonObject{
           {QStringLiteral("role"), QStringLiteral("assistant")},
           {QStringLiteral("content"), QString()},
           {QStringLiteral("thinking"),
            QString::fromUtf8(
                QJsonDocument(scope_misread_plan)
                    .toJson(QJsonDocument::Compact))},
       }},
  };
  if (fake_ollama_server_available) {
    fake_ollama_server.setChunkedResponse(
        QStringLiteral("/api/chat"), scope_misread_response);
  } else {
    qputenv(
        "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
        QJsonDocument(scope_misread_response).toJson(QJsonDocument::Compact));
  }
  auto global_scope_request = request;
  global_scope_request.user_instructions = QStringLiteral(
      "Create a polished warm overall look. Keep the framing unchanged and protect highlights.");
  const auto global_scope_plan =
      photo_editor::desktop::plan_agent_recipe_patch(global_scope_request);
  const auto scope_correction =
      global_scope_plan.prompt_audit
          .value(QStringLiteral("globalChangePolicyCorrection"))
          .toMap();
  if (!global_scope_plan.ok || global_scope_plan.recipe_patch.isEmpty() ||
      global_scope_plan.edit_intent
              .value(QStringLiteral("globalChangePolicy"))
              .toString() != QStringLiteral("allow") ||
      scope_correction.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-explicit-scope-authority-v1") ||
      scope_correction.value(QStringLiteral("modelValue")).toString() !=
          QStringLiteral("preserve") ||
      scope_correction.value(QStringLiteral("enforcedValue")).toString() !=
          QStringLiteral("allow") ||
      !scope_correction
           .value(QStringLiteral("currentRequestAuthoritative"))
           .toBool()) {
    return fail("an explicit overall look should remain global when the model mistakes framing or highlight protection for local-only scope");
  }

  const QJsonObject calibrated_critique_content{
      {QStringLiteral("pass"), true},
      {QStringLiteral("summary"),
       QStringLiteral("The restrained warmth remains natural.")},
      {QStringLiteral("issues"), QJsonArray{}},
      {QStringLiteral("repairHints"), QJsonArray{}},
      {QStringLiteral("needsUserReview"), true},
      {QStringLiteral("tasteConfidence"), QStringLiteral("medium")},
  };
  const QJsonObject calibrated_critique_response{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"),
       QJsonObject{
           {QStringLiteral("role"), QStringLiteral("assistant")},
           {QStringLiteral("content"),
            QString::fromUtf8(
                QJsonDocument(calibrated_critique_content)
                    .toJson(QJsonDocument::Compact))},
       }},
  };
  if (fake_ollama_server_available) {
    fake_ollama_server.setChunkedResponse(
        QStringLiteral("/api/chat"), calibrated_critique_response);
  } else {
    qputenv(
        "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
        QJsonDocument(calibrated_critique_response)
            .toJson(QJsonDocument::Compact));
  }
  photo_editor::desktop::AgentLocalModelProofCritiqueRequest critique_request;
  critique_request.model_id = QStringLiteral("ollama-qwen-test");
  critique_request.style = learned_warm_style;
  critique_request.asset = QVariantMap{
      {QStringLiteral("proofImagePath"), fake_proof_path},
  };
  critique_request.user_instructions =
      QStringLiteral("Keep the edit warm but natural.");
  critique_request.graphic_layers = QVariantList{QVariantMap{
      {QStringLiteral("name"), QStringLiteral("Poster headline")},
      {QStringLiteral("text"), QStringLiteral("LOOKSY FIELD NOTES")},
      {QStringLiteral("x"), 96},
      {QStringLiteral("y"), 88},
      {QStringLiteral("pixelWidth"), 900},
      {QStringLiteral("pixelHeight"), 180},
      {QStringLiteral("canvasWidth"), 1200},
      {QStringLiteral("canvasHeight"), 1200},
  }};
  critique_request.validation_report = QVariantMap{
      {QStringLiteral("ok"), true},
      {QStringLiteral("imageStats"),
       QVariantMap{{QStringLiteral("vectorscope"), QVariantMap{}}}},
  };
  critique_request.validation_report.insert(QStringLiteral("masks"), QVariantList{QVariantMap{
      {QStringLiteral("segmentationData"), QByteArray(1024 * 1024, 'Z')},
      {QStringLiteral("generatedMaskCleanupSourceData"), QString(1024 * 1024, QLatin1Char('Z'))},
      {QStringLiteral("temperature"), 12.0}}});
  critique_request.photographer_calibration =
      request.memory_packet
          .value(QStringLiteral("photographerCalibrationCard"))
          .toMap();
  critique_request.project_north_star =
      request.memory_packet.value(QStringLiteral("projectNorthStar")).toMap();
  const auto calibrated_critique =
      photo_editor::desktop::critique_agent_rendered_proof(
          critique_request);
  const auto calibrated_critique_request =
      fake_ollama_server_available
          ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
          : read_json_file(fake_ollama_request_path);
  const auto calibrated_critique_messages =
      calibrated_critique_request.value(QStringLiteral("messages"))
          .toArray();
  const auto calibrated_critique_prompt =
      calibrated_critique_messages.size() >= 2
          ? calibrated_critique_messages.at(1)
                .toObject()
                .value(QStringLiteral("content"))
                .toString()
          : QString();
  if (calibrated_critique_prompt.size() > 48000 || calibrated_critique_prompt.contains(QString(64, QLatin1Char('Z'))) ||
      !calibrated_critique_prompt.contains(QStringLiteral("payloadOmitted")))
    return fail("proof critique must receive compact metadata, never stored alpha bytes as prompt text");
  auto missing_proof_request = critique_request;
  missing_proof_request.asset = {{QStringLiteral("proofImagePath"), QStringLiteral("/missing-rendered-proof.png")},
                                 {QStringLiteral("filePath"), fake_proof_path}};
  const auto missing_proof_critique = photo_editor::desktop::critique_agent_rendered_proof(missing_proof_request);
  if (missing_proof_critique.ok || missing_proof_critique.pass || !missing_proof_critique.needs_user_review)
    return fail("a missing rendered proof must not be silently replaced with the original image for critique");
  if (!calibrated_critique.ok || !calibrated_critique.pass ||
      !calibrated_critique.needs_user_review ||
      !calibrated_critique.prompt_audit
           .value(QStringLiteral("photographerCalibrationAvailable"))
           .toBool() ||
      calibrated_critique.prompt_audit
              .value(QStringLiteral("photographerCalibrationDigest"))
              .toString() != QString(64, QLatin1Char('b')) ||
      !calibrated_critique.prompt_audit
           .value(QStringLiteral("projectNorthStarAvailable"))
           .toBool() ||
      calibrated_critique.prompt_audit
              .value(QStringLiteral("projectNorthStarDigest"))
              .toString() != QString(64, QLatin1Char('c')) ||
      !calibrated_critique.prompt_audit
           .value(QStringLiteral("projectNorthStarCurrentRequestWins"))
           .toBool() ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("Photographer calibration JSON")) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("Prioritize naturalness and use restrained warmth")) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("never change numeric thresholds"),
          Qt::CaseInsensitive) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("Project North Star JSON")) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("Keep this wedding intimate and documentary")) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("current request, active style constraints, deterministic validation, and safety policy win every conflict"),
          Qt::CaseInsensitive) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("compare every expected text string"),
          Qt::CaseInsensitive) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("LOOKSY FIELD NOTES")) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("Expected graphic text layers JSON"))) {
    return fail("vision proof critique should consume project calibration and the North Star as qualitative evidence while retaining current-request precedence");
  }

  const auto install_critique_response = [&](const QJsonObject& content,
                                            bool done = true,
                                            const QString& done_reason = QStringLiteral("stop")) {
    auto response = calibrated_critique_response;
    auto message = response.value(QStringLiteral("message")).toObject();
    message.insert(QStringLiteral("content"), QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact)));
    response.insert(QStringLiteral("message"), message);
    response.insert(QStringLiteral("done"), done);
    response.insert(QStringLiteral("done_reason"), done_reason);
    response.insert(QStringLiteral("total_duration"), 123000000.0);
    response.insert(QStringLiteral("prompt_eval_duration"), 42000000.0);
    response.insert(QStringLiteral("prompt_eval_count"), 7000);
    response.insert(QStringLiteral("prompt_eval_cached_count"), 6900);
    if (fake_ollama_server_available)
      fake_ollama_server.setChunkedResponse(QStringLiteral("/api/chat"), response);
    else
      qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON", QJsonDocument(response).toJson(QJsonDocument::Compact));
  };
  auto clean_critique_content = calibrated_critique_content;
  clean_critique_content.insert(QStringLiteral("needsUserReview"), false);
  for (const auto& key : {"pass", "needsUserReview", "summary", "issues", "repairHints", "tasteConfidence"}) {
    auto malformed = clean_critique_content;
    malformed.remove(QLatin1String(key));
    install_critique_response(malformed);
    const auto result = photo_editor::desktop::critique_agent_rendered_proof(critique_request);
    if (result.ok || result.pass || !result.needs_user_review || result.error_message.isEmpty())
      return fail("a missing required critique field must fail closed before publishing pass or review state");
  }
  for (const auto& key : {"issues", "repairHints", "warnings"}) {
    auto malformed = clean_critique_content;
    malformed.insert(QLatin1String(key), QJsonArray{QJsonObject{{QStringLiteral("hiddenIssue"), true}}});
    install_critique_response(malformed);
    const auto result = photo_editor::desktop::critique_agent_rendered_proof(critique_request);
    if (result.ok || result.pass || !result.needs_user_review)
      return fail("non-string critique issues must not be silently discarded by the UI");
  }
  for (const auto& key : {"pass", "needsUserReview"}) {
    auto malformed = clean_critique_content;
    malformed.insert(QLatin1String(key), QStringLiteral("true"));
    install_critique_response(malformed);
    const auto result = photo_editor::desktop::critique_agent_rendered_proof(critique_request);
    if (result.ok || result.pass || !result.needs_user_review)
      return fail("critique booleans must not be coerced from strings");
  }
  for (const auto truncated : {false, true}) {
    install_critique_response(clean_critique_content, truncated,
        truncated ? QStringLiteral("length") : QStringLiteral("stop"));
    const auto result = photo_editor::desktop::critique_agent_rendered_proof(critique_request);
    if (result.ok || result.pass || !result.needs_user_review)
      return fail("unfinished or token-limited model output is not completed proof verification");
  }
  install_critique_response(clean_critique_content);
  auto unresolved_request = critique_request;
  unresolved_request.validation_report.insert(QStringLiteral("needsUserReview"), true);
  const auto unresolved = photo_editor::desktop::critique_agent_rendered_proof(unresolved_request);
  if (!unresolved.ok || !unresolved.needs_user_review)
    return fail("the critic cannot waive deterministic human-review requirements");
  unresolved_request.validation_report.insert(QStringLiteral("ok"), false);
  const auto failed_validation = photo_editor::desktop::critique_agent_rendered_proof(unresolved_request);
  if (!failed_validation.ok || failed_validation.pass || !failed_validation.needs_user_review)
    return fail("a model approval cannot override failed deterministic validation");
  const auto clean_critique = photo_editor::desktop::critique_agent_rendered_proof(critique_request);
  const auto timings = clean_critique.prompt_audit.value(QStringLiteral("ollamaServer")).toMap();
  if (!clean_critique.ok || !clean_critique.pass || clean_critique.needs_user_review ||
      timings.value(QStringLiteral("totalDurationMs")).toInt() != 123 ||
      timings.value(QStringLiteral("promptEvalDurationMs")).toInt() != 42 ||
      timings.value(QStringLiteral("promptEvalCachedCount")).toInt() != 6900 ||
      timings.contains(QStringLiteral("evalDurationMs")))
    return fail("completed critiques must report real timing/cache evidence without inventing missing metrics");
  auto omitted_evidence_request = critique_request;
  omitted_evidence_request.photographer_calibration.insert(
      QStringLiteral("qualitativeNotes"), QString(5000, QLatin1Char('x')));
  const auto omitted_evidence = photo_editor::desktop::critique_agent_rendered_proof(omitted_evidence_request);
  if (!omitted_evidence.ok || omitted_evidence.pass || !omitted_evidence.needs_user_review ||
      omitted_evidence.prompt_audit.value(QStringLiteral("metadataEvidenceComplete")).toBool())
    return fail("a model must not claim verification when required qualitative evidence was truncated");
  auto contradictory = clean_critique_content;
  contradictory.insert(QStringLiteral("issues"), QJsonArray{QStringLiteral("Skin needs inspection")});
  install_critique_response(contradictory);
  const auto contradicted = photo_editor::desktop::critique_agent_rendered_proof(critique_request);
  if (!contradicted.ok || !contradicted.needs_user_review)
    return fail("model issues cannot coexist with a waived review requirement");

  auto object_model_plan = model_plan;
  auto object_edit_intent =
      object_model_plan.value(QStringLiteral("editIntent")).toObject();
  object_edit_intent.insert(
      QStringLiteral("visualFindings"),
      QJsonArray{
          QJsonObject{
              {QStringLiteral("kind"), QStringLiteral("localized_color")},
              {QStringLiteral("target"), QStringLiteral("object")},
              {QStringLiteral("targetLabel"), QStringLiteral("red jacket")},
              {QStringLiteral("severity"), QStringLiteral("medium")},
              {QStringLiteral("observation"),
               QStringLiteral("The red jacket is darker than the surrounding scene.")},
              {QStringLiteral("confidence"), 0.90},
              {QStringLiteral("region"),
               QJsonObject{
                   {QStringLiteral("centerX"), 0.42},
                   {QStringLiteral("centerY"), 0.56},
                   {QStringLiteral("radiusX"), 0.16},
                   {QStringLiteral("radiusY"), 0.24},
               }},
          },
      });
  object_edit_intent.insert(
      QStringLiteral("localizedActions"),
      QJsonArray{
          QJsonObject{
              {QStringLiteral("target"), QStringLiteral("object")},
              {QStringLiteral("targetLabel"), QStringLiteral("red jacket")},
              {QStringLiteral("maskShape"), QStringLiteral("radial")},
              {QStringLiteral("selectionMode"), QStringLiteral("guided")},
              {QStringLiteral("selectionCandidates"),
               QJsonArray{
                   QJsonObject{
                       {QStringLiteral("label"), QStringLiteral("Left red jacket")},
                       {QStringLiteral("confidence"), 0.76},
                       {QStringLiteral("region"),
                        QJsonObject{
                            {QStringLiteral("centerX"), 0.32},
                            {QStringLiteral("centerY"), 0.56},
                            {QStringLiteral("radiusX"), 0.12},
                            {QStringLiteral("radiusY"), 0.22},
                            {QStringLiteral("feather"), 0.70},
                        }},
                   },
                   QJsonObject{
                       {QStringLiteral("label"), QStringLiteral("Right red jacket")},
                       {QStringLiteral("confidence"), 0.72},
                       {QStringLiteral("region"),
                        QJsonObject{
                            {QStringLiteral("centerX"), 0.62},
                            {QStringLiteral("centerY"), 0.54},
                            {QStringLiteral("radiusX"), 0.13},
                            {QStringLiteral("radiusY"), 0.23},
                            {QStringLiteral("feather"), 0.70},
                        }},
                   },
               }},
              {QStringLiteral("reason"),
               QStringLiteral("Lift only the visible red jacket.")},
              {QStringLiteral("confidence"), 0.89},
              {QStringLiteral("region"),
               QJsonObject{
                   {QStringLiteral("centerX"), 0.42},
                   {QStringLiteral("centerY"), 0.56},
                   {QStringLiteral("radiusX"), 0.16},
                   {QStringLiteral("radiusY"), 0.24},
                   {QStringLiteral("feather"), 0.70},
               }},
              {QStringLiteral("adjustments"),
               QJsonObject{{QStringLiteral("exposure"), 0.20}}},
          },
      });
  object_model_plan.insert(QStringLiteral("editIntent"), object_edit_intent);
  object_model_plan.insert(
      QStringLiteral("summary"),
      QStringLiteral("Lift only the visible red jacket"));
  const QJsonObject fake_object_response{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"),
       QJsonObject{
           {QStringLiteral("role"), QStringLiteral("assistant")},
           {QStringLiteral("content"), QString()},
           {QStringLiteral("thinking"),
            QString::fromUtf8(
                QJsonDocument(object_model_plan).toJson(QJsonDocument::Compact))},
       }},
  };
  if (fake_ollama_server_available) {
    fake_ollama_server.setChunkedResponse(
        QStringLiteral("/api/chat"), fake_object_response);
  } else {
    qputenv(
        "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
        QJsonDocument(fake_object_response).toJson(QJsonDocument::Compact));
  }
  const auto object_plan =
      photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto object_findings =
      object_plan.edit_intent.value(QStringLiteral("visualFindings")).toList();
  const auto object_actions =
      object_plan.edit_intent.value(QStringLiteral("localizedActions")).toList();
  const auto object_masks = object_plan.recipe_patch
                                .value(QStringLiteral("masks"))
                                .toMap()
                                .value(QStringLiteral("appendMaskStack"))
                                .toList();
  const auto object_finding = object_findings.isEmpty()
                                  ? QVariantMap{}
                                  : object_findings.first().toMap();
  const auto object_action = object_actions.isEmpty()
                                 ? QVariantMap{}
                                 : object_actions.first().toMap();
  const auto object_mask = object_masks.isEmpty()
                               ? QVariantMap{}
                               : object_masks.first().toMap();
  const auto object_mask_id =
      object_action.value(QStringLiteral("maskId")).toString();
  const auto object_candidates =
      object_mask.value(QStringLiteral("agentSelectionCandidates")).toList();
  if (!object_plan.ok ||
      object_finding.value(QStringLiteral("target")).toString() !=
          QStringLiteral("object") ||
      object_finding.value(QStringLiteral("targetLabel")).toString() !=
          QStringLiteral("red jacket") ||
      object_action.value(QStringLiteral("target")).toString() !=
          QStringLiteral("object") ||
      object_action.value(QStringLiteral("targetLabel")).toString() !=
          QStringLiteral("red jacket") ||
      object_action.value(QStringLiteral("selectionMode")).toString() !=
          QStringLiteral("guided") ||
      object_mask_id.isEmpty() ||
      object_finding.value(QStringLiteral("maskId")).toString() != object_mask_id ||
      object_mask.value(QStringLiteral("id")).toString() != object_mask_id ||
      object_mask.value(QStringLiteral("kind")).toString() !=
          QStringLiteral("radial") ||
      object_mask.value(QStringLiteral("agentSelectionMode")).toString() !=
          QStringLiteral("guided") ||
      object_mask.value(QStringLiteral("agentSelectionLabel")).toString() !=
          QStringLiteral("Red jacket") ||
      object_candidates.size() != 2 ||
      object_candidates.at(0).toMap().value(QStringLiteral("id")).toString().isEmpty() ||
      object_candidates.at(0).toMap().value(QStringLiteral("id")).toString() ==
          object_candidates.at(1).toMap().value(QStringLiteral("id")).toString() ||
      object_candidates.at(1).toMap().value(QStringLiteral("label")).toString() !=
          QStringLiteral("Right red jacket") ||
      object_mask.contains(QStringLiteral("agentSemanticTarget"))) {
    std::cerr << "object plan="
              << QJsonDocument::fromVariant(QVariantMap{
                     {QStringLiteral("ok"), object_plan.ok},
                     {QStringLiteral("finding"), object_finding},
                     {QStringLiteral("action"), object_action},
                     {QStringLiteral("mask"), object_mask},
                     {QStringLiteral("audit"), object_plan.prompt_audit},
                 })
                     .toJson(QJsonDocument::Compact)
                     .toStdString()
              << '\n';
    return fail("Ollama open-vocabulary findings should compile into linked, labeled, evidence-guided object-mask requests");
  }
  const auto object_validation =
      photo_editor::desktop::validate_agent_recipe_patch(
          object_plan.recipe_patch, warm_style);
  if (!object_validation.valid) {
    std::cerr << object_validation.errors.join(QStringLiteral("; ")).toStdString()
              << '\n';
    return fail("open-vocabulary guided object patches should satisfy recipe validation");
  }

  const auto flat_highlight_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/no-highlight.png"));
  QImage flat_highlight_fixture(800, 600, QImage::Format_RGB32);
  flat_highlight_fixture.fill(QColor(88, 92, 96));
  if (!flat_highlight_fixture.save(flat_highlight_path)) {
    return fail("flat fast-path rejection fixture should be writable");
  }
  auto no_evidence_request = request;
  no_evidence_request.strategy_phase = QStringLiteral("sample");
  no_evidence_request.asset = QVariantMap{
      {QStringLiteral("assetId"), QStringLiteral("no-highlight")},
      {QStringLiteral("proofImagePath"), flat_highlight_path},
  };
  no_evidence_request.user_instructions = QStringLiteral(
      "Subdue only the bright upper-right window and leave the rest unchanged.");
  const auto no_evidence_plan =
      photo_editor::desktop::plan_agent_recipe_patch(no_evidence_request);
  if (!no_evidence_plan.ok ||
      no_evidence_plan.model_id != QStringLiteral("ollama-qwen-test") ||
      no_evidence_plan.prompt_audit.value(QStringLiteral("backend")).toString() !=
          QStringLiteral("ollama") ||
      !no_evidence_plan.prompt_audit.value(QStringLiteral("plannerMode")).toString().isEmpty()) {
    return fail("fast local vision should defer to the selected model when bright-region evidence is absent");
  }

  const QJsonObject model_tool_plan{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Need live status before planning.")},
      {QStringLiteral("toolCalls"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("name"), QStringLiteral("photo.live_status")},
               {QStringLiteral("arguments"), QJsonObject{}},
               {QStringLiteral("reason"), QStringLiteral("Read current editor state.")},
           },
       }},
  };
  const QJsonObject fake_tool_plan_response{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"),
       QJsonObject{
           {QStringLiteral("role"), QStringLiteral("assistant")},
           {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(model_tool_plan).toJson(QJsonDocument::Compact))},
       }},
  };
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      QJsonDocument(fake_tool_plan_response).toJson(QJsonDocument::Compact));
  request.allow_tool_call_plan = true;
  request.available_tool_schemas =
      QVariantList{QVariantMap{{QStringLiteral("name"), QStringLiteral("photo.live_status")}}};
  const auto ollama_tool_plan = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (!ollama_tool_plan.ok ||
      ollama_tool_plan.decision != QStringLiteral("tool_call_plan") ||
      ollama_tool_plan.tool_call_plan.size() != 1 ||
      ollama_tool_plan.tool_call_plan.first().toMap().value(QStringLiteral("name")).toString() !=
          QStringLiteral("photo.live_status") ||
      !ollama_tool_plan.recipe_patch.isEmpty()) {
    return fail("Ollama backend should parse a structured tool_call_plan when tool calls are allowed");
  }
  request.allow_tool_call_plan = false;
  request.available_tool_schemas = {};

  const QJsonObject needs_input_plan{
      {QStringLiteral("decision"), QStringLiteral("needs_user_input")},
      {QStringLiteral("summary"), QStringLiteral("Need the target mood before editing.")},
      {QStringLiteral("userQuestions"),
       QJsonArray{QStringLiteral("Should the set lean warm editorial or neutral documentary?")}},
  };
  const QJsonObject fake_needs_input_response{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"),
       QJsonObject{
           {QStringLiteral("role"), QStringLiteral("assistant")},
           {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(needs_input_plan).toJson(QJsonDocument::Compact))},
       }},
  };
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      QJsonDocument(fake_needs_input_response).toJson(QJsonDocument::Compact));
  const auto ollama_needs_input = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (!ollama_needs_input.ok ||
      ollama_needs_input.decision != QStringLiteral("needs_user_input") ||
      ollama_needs_input.user_questions.size() != 1 ||
      !ollama_needs_input.recipe_patch.isEmpty()) {
    return fail("Ollama backend should parse needs_user_input as a safe non-draft decision");
  }

  const QJsonObject cannot_complete_plan{
      {QStringLiteral("decision"), QStringLiteral("cannot_complete_safely")},
      {QStringLiteral("summary"), QStringLiteral("Requested retouch exceeds the active rulebook.")},
      {QStringLiteral("reason"), QStringLiteral("The selected style disallows retouch and inpaint tools.")},
  };
  const QJsonObject fake_cannot_complete_response{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"),
       QJsonObject{
           {QStringLiteral("role"), QStringLiteral("assistant")},
           {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(cannot_complete_plan).toJson(QJsonDocument::Compact))},
       }},
  };
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      QJsonDocument(fake_cannot_complete_response).toJson(QJsonDocument::Compact));
  const auto ollama_cannot_complete = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (!ollama_cannot_complete.ok ||
      ollama_cannot_complete.decision != QStringLiteral("cannot_complete_safely") ||
      ollama_cannot_complete.safe_stop_reason !=
          QStringLiteral("The selected style disallows retouch and inpaint tools.") ||
      !ollama_cannot_complete.recipe_patch.isEmpty()) {
    return fail("Ollama backend should parse cannot_complete_safely as a safe non-draft decision");
  }

  const QJsonObject incomplete_recipe_plan{
      {QStringLiteral("decision"), QStringLiteral("edit_intent")},
      {QStringLiteral("summary"), QStringLiteral("Natural portrait direction without a completed intent.")},
      {QStringLiteral("styleConstraintCitations"),
       QJsonArray{QJsonObject{
           {QStringLiteral("field"), QStringLiteral("constraints.max_exposure_shift")},
           {QStringLiteral("reason"), QStringLiteral("Keep the edit conservative.")},
       }}},
      {QStringLiteral("warnings"), QJsonArray{}},
  };
  const QJsonObject fake_incomplete_recipe_response{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"),
       QJsonObject{
           {QStringLiteral("role"), QStringLiteral("assistant")},
           {QStringLiteral("content"),
            QString::fromUtf8(
                QJsonDocument(incomplete_recipe_plan).toJson(QJsonDocument::Compact))},
       }},
  };
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      QJsonDocument(fake_incomplete_recipe_response).toJson(QJsonDocument::Compact));
  request.user_instructions = QStringLiteral("Make it bright and colorful.");
  const auto recovered_incomplete_plan =
      photo_editor::desktop::plan_agent_recipe_patch(request);
  if (!recovered_incomplete_plan.ok || recovered_incomplete_plan.recipe_patch.isEmpty() ||
      !recovered_incomplete_plan.prompt_audit.value(QStringLiteral("fallbackUsed")).toBool() ||
      recovered_incomplete_plan.prompt_audit.value(QStringLiteral("requestedModelId")).toString() !=
          QStringLiteral("ollama-qwen-test") ||
      recovered_incomplete_plan.warnings.isEmpty()) {
    return fail("incomplete Ollama recipe responses should automatically produce a safe deterministic draft");
  }
  request.user_instructions = QStringLiteral("Warm only the entire background and keep the people unchanged.");
  const auto semantic_command = photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto command_masks = semantic_command.recipe_patch.value(QStringLiteral("masks")).toMap().value(QStringLiteral("appendMaskStack")).toList();
  if (!semantic_command.ok || semantic_command.recipe_patch.size() != 1 || command_masks.size() != 1 ||
      semantic_command.prompt_audit.value(QStringLiteral("modelInvoked")).toBool() ||
      semantic_command.prompt_audit.value(QStringLiteral("plannerMode")).toString() != QStringLiteral("semantic-command-v1") ||
      command_masks.first().toMap().value(QStringLiteral("agentSemanticTarget")).toString() != QStringLiteral("background") ||
      !command_masks.first().toMap().value(QStringLiteral("agentRequireSemanticMask")).toBool() ||
      command_masks.first().toMap().value(QStringLiteral("temperature")).toDouble() != 12.0 ||
      !photo_editor::desktop::validate_agent_recipe_patch(semantic_command.recipe_patch, request.style).valid)
    return fail("a complete simple local command should compile only a required semantic mask with editable adjustments");
  request.user_instructions = QStringLiteral("Warm the background and brighten the face.");
  const auto compound_command = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (compound_command.ok || compound_command.prompt_audit.value(QStringLiteral("plannerMode")).toString() == QStringLiteral("semantic-command-v1"))
    return fail("the fast semantic grammar must not discard extra requested edits");
  request.user_instructions = QStringLiteral("Warm only the entire background and keep the people unchanged.");
  request.force_model_planning = true;
  const auto protected_failure = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (protected_failure.ok || !protected_failure.recipe_patch.isEmpty() ||
      !protected_failure.prompt_audit.value(QStringLiteral("fallbackBlocked")).toBool())
    return fail("a failed localized planner must never become a global deterministic preset");
  request.force_model_planning = false;
  request.user_instructions = QStringLiteral("Make it bright and colorful.");
  request.allow_deterministic_fallback = false;
  const auto confirmed_failure = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (confirmed_failure.ok || !confirmed_failure.recipe_patch.isEmpty())
    return fail("confirmed search/edit requests must not silently substitute another planner");
  request.allow_deterministic_fallback = true;

  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  if (fake_ollama_server_available) {
    fake_ollama_server.setErrorResponse(
        QStringLiteral("/api/chat"),
        503,
        QJsonObject{{QStringLiteral("error"), QStringLiteral("model worker unavailable")}});
    const auto recovered_http_failure =
        photo_editor::desktop::plan_agent_recipe_patch(request);
    const auto failed_transport =
        recovered_http_failure.prompt_audit.value(QStringLiteral("ollamaAttempt"))
            .toMap()
            .value(QStringLiteral("transport"))
            .toMap();
    if (!recovered_http_failure.ok || recovered_http_failure.recipe_patch.isEmpty() ||
        !recovered_http_failure.prompt_audit.value(QStringLiteral("fallbackUsed")).toBool() ||
        failed_transport.value(QStringLiteral("errorCode")).toString() !=
            QStringLiteral("http_status") ||
        failed_transport.value(QStringLiteral("statusCode")).toInt() != 503) {
      std::cerr << "Ollama HTTP recovery result ok="
                << recovered_http_failure.ok
                << " recipeEmpty="
                << recovered_http_failure.recipe_patch.isEmpty()
                << " error=\""
                << recovered_http_failure.error_message.toStdString()
                << "\" audit="
                << QJsonDocument::fromVariant(
                       recovered_http_failure.prompt_audit)
                       .toJson(QJsonDocument::Compact)
                       .toStdString()
                << '\n';
      return fail("Ollama HTTP failures should automatically produce a safe deterministic draft");
    }
  }

  request.model_id = QStringLiteral("missing-model");
  const auto missing_model_plan = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (missing_model_plan.ok || missing_model_plan.error_message.isEmpty()) {
    return fail("unsupported local model ids should be rejected");
  }

  // Managed readiness must not rewrite explicit external endpoint authority.
  if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail("managed manifest fixture");
  manifest_file.write(R"json({"models":[
    {"id":"managed-test","backend":"ollama","runtime":"managed","model":"fixture:vision","vision":true},
    {"id":"external-test","backend":"ollama","endpoint":"http://127.0.0.1:11434","model":"external:vision","vision":true}
  ]})json");
  manifest_file.close();
  photo_editor::desktop::publish_managed_agent_runtime({{"ready", false}, {"state", "starting"}});
  auto managed = model_by_id(photo_editor::desktop::available_agent_local_models(), "managed-test");
  if (managed.value("available").toBool() || !managed.value("endpoint").toString().isEmpty() ||
      photo_editor::desktop::default_agent_local_model_id() != "managed-test")
    return fail("a managed model must remain selected but unavailable while its owned runtime starts");
  photo_editor::desktop::publish_managed_agent_runtime({{"ready", true}, {"state", "ready"},
      {"endpoint", "http://127.0.0.1:32123"}, {"installedModels", QStringList{}}});
  managed = model_by_id(photo_editor::desktop::available_agent_local_models(), "managed-test");
  if (managed.value("available").toBool() || !managed.value("statusReason").toString().contains("Download"))
    return fail("server readiness alone does not prove model installation");
  photo_editor::desktop::publish_managed_agent_runtime({{"ready", true}, {"state", "ready"},
      {"endpoint", "http://127.0.0.1:32123"}, {"installedModels", QStringList{"fixture:vision"}}});
  managed = model_by_id(photo_editor::desktop::available_agent_local_models(), "managed-test");
  if (managed.value("available").toBool() ||
      photo_editor::desktop::probe_agent_local_model("managed-test").value("ok").toBool())
    return fail("an installed managed model must not bypass artifact verification through a probe");
  photo_editor::desktop::publish_managed_agent_runtime({{"ready", true}, {"state", "ready"},
      {"endpoint", "http://127.0.0.1:32123"}, {"installedModels", QStringList{"fixture:vision"}},
      {"verifiedModels", QStringList{"fixture:vision"}}});
  const auto managed_models = photo_editor::desktop::available_agent_local_models();
  managed = model_by_id(managed_models, "managed-test");
  if (!managed.value("available").toBool() || managed.value("endpoint").toString() != "http://127.0.0.1:32123" ||
      model_by_id(managed_models, "external-test").value("endpoint").toString() != "http://127.0.0.1:11434")
    return fail("owned runtime endpoint routing must leave external model configurations untouched");
  photo_editor::desktop::publish_managed_agent_runtime({});
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH");
  qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_RUN_LIVE_OLLAMA_SMOKE")) {
    const auto live_app_data_root =
        qEnvironmentVariable("PHOTO_EDITOR_LIVE_OLLAMA_APP_DATA_DIR").trimmed();
    const auto live_model_id =
        qEnvironmentVariable(
            "PHOTO_EDITOR_LIVE_OLLAMA_MODEL_ID",
            QStringLiteral("ollama-qwen3-vl-32b"))
            .trimmed();
    if (live_app_data_root.isEmpty()) {
      return fail("live Ollama smoke requires PHOTO_EDITOR_LIVE_OLLAMA_APP_DATA_DIR");
    }
    qputenv("PHOTO_EDITOR_APP_DATA_DIR", live_app_data_root.toUtf8());

    const auto live_probe =
        photo_editor::desktop::probe_agent_local_model(live_model_id);
    if (!live_probe.value(QStringLiteral("ok")).toBool() ||
        !live_probe.value(QStringLiteral("available")).toBool() ||
        live_probe.value(QStringLiteral("backend")).toString() !=
            QStringLiteral("ollama")) {
      std::cerr << "live Ollama probe failed: "
                << QJsonDocument::fromVariant(live_probe)
                       .toJson(QJsonDocument::Compact)
                       .toStdString()
                << '\n';
      return fail("configured live Ollama vision model should be reachable");
    }

    photo_editor::desktop::AgentLocalModelPlanRequest live_request;
    live_request.model_id = live_model_id;
    live_request.style = warm_style;
    live_request.asset = QVariantMap{
        {QStringLiteral("assetId"), QStringLiteral("live-runtime-smoke")},
        {QStringLiteral("filePath"), fast_highlight_path},
    };
    live_request.current_adjustment_patch_json = QStringLiteral(
        "{\"kind\":\"photo-editor-adjustment-patch\",\"version\":1,"
        "\"scope\":\"asset-bound\",\"groups\":{}}");
    live_request.user_instructions = QStringLiteral(
        "Create a restrained cinematic evening look with gentle contrast and "
        "natural color. Keep the framing unchanged and protect the brightest "
        "area from clipping.");
    live_request.strategy_phase = QStringLiteral("sample");
    live_request.force_model_planning = true;

    QElapsedTimer live_plan_timer;
    live_plan_timer.start();
    const auto live_plan =
        photo_editor::desktop::plan_agent_recipe_patch(live_request);
    const auto live_plan_elapsed_ms = live_plan_timer.elapsed();
    const auto live_plan_transport =
        live_plan.prompt_audit.value(QStringLiteral("transport")).toMap();
    const auto live_generation_schema_contract =
        live_plan.prompt_audit
            .value(QStringLiteral("generationSchemaContract"))
            .toString();
    const auto live_generation_schema_byte_count =
        live_plan.prompt_audit
            .value(QStringLiteral("generationSchemaByteCount"))
            .toInt();
    const auto live_scope_correction =
        live_plan.prompt_audit
            .value(QStringLiteral("globalChangePolicyCorrection"))
            .toMap();
    if (!live_plan.ok || live_plan.recipe_patch.isEmpty() ||
        live_plan.edit_intent.isEmpty() ||
        live_plan.prompt_audit.value(QStringLiteral("backend")).toString() !=
            QStringLiteral("ollama") ||
        live_plan.prompt_audit.value(QStringLiteral("fallbackUsed")).toBool() ||
        !live_plan.prompt_audit.value(QStringLiteral("imageIncluded")).toBool() ||
        live_generation_schema_contract !=
            QStringLiteral("agent-compact-semantic-intent-generation-v1") ||
        live_generation_schema_byte_count <= 0 ||
        live_generation_schema_byte_count >= 2048 ||
        live_plan.edit_intent.value(QStringLiteral("globalChangePolicy")).toString() !=
            QStringLiteral("allow") ||
        (!live_scope_correction.isEmpty() &&
         (live_scope_correction.value(QStringLiteral("contract")).toString() !=
              QStringLiteral("agent-explicit-scope-authority-v1") ||
          !live_scope_correction
               .value(QStringLiteral("currentRequestAuthoritative"))
               .toBool()))) {
      std::cerr << "live Ollama planning failed: "
                << live_plan.error_message.toStdString() << " audit="
                << QJsonDocument::fromVariant(live_plan.prompt_audit)
                       .toJson(QJsonDocument::Compact)
                       .toStdString()
                << '\n';
      return fail("live Ollama should return one usable image-grounded edit plan without fallback");
    }

    photo_editor::desktop::AgentLocalModelProofCritiqueRequest critique_request;
    critique_request.model_id = live_model_id;
    critique_request.style = warm_style;
    critique_request.asset = live_request.asset;
    // This smoke fixture is explicitly the proof under review, not a fallback
    // from a missing production after image to an original photograph.
    critique_request.asset.insert(QStringLiteral("proofImagePath"), fast_highlight_path);
    critique_request.user_instructions = live_request.user_instructions;
    critique_request.validation_report = QVariantMap{
        {QStringLiteral("ok"), true},
        {QStringLiteral("intentMatch"),
         QVariantMap{
             {QStringLiteral("available"), true},
             {QStringLiteral("ok"), true},
             {QStringLiteral("scorePercent"), 96},
         }},
        {QStringLiteral("warnings"), QVariantList{}},
        {QStringLiteral("errors"), QVariantList{}},
    };
    QElapsedTimer live_critique_timer;
    live_critique_timer.start();
    const auto live_critique =
        photo_editor::desktop::critique_agent_rendered_proof(
            critique_request);
    const auto live_critique_elapsed_ms = live_critique_timer.elapsed();
    if (!live_critique.ok || live_critique.summary.trimmed().isEmpty() ||
        live_critique.taste_confidence.trimmed().isEmpty() ||
        live_critique.prompt_audit.value(QStringLiteral("backend")).toString() !=
            QStringLiteral("ollama") ||
        !live_critique.prompt_audit
             .value(QStringLiteral("imageIncluded"))
             .toBool()) {
      std::cerr << "live Ollama proof critique failed: "
                << live_critique.error_message.toStdString() << " audit="
                << QJsonDocument::fromVariant(live_critique.prompt_audit)
                       .toJson(QJsonDocument::Compact)
                       .toStdString()
                << '\n';
      return fail("live Ollama should return one usable image-grounded proof critique");
    }

    const QVariantMap live_receipt{
        {QStringLiteral("contract"),
         QStringLiteral("agent-live-local-runtime-smoke-v1")},
        {QStringLiteral("modelId"), live_model_id},
        {QStringLiteral("ollamaModel"),
         live_probe.value(QStringLiteral("ollamaModel"))},
        {QStringLiteral("modelFamily"),
         live_probe.value(QStringLiteral("modelFamily"))},
        {QStringLiteral("parameterSize"),
         live_probe.value(QStringLiteral("parameterSize"))},
        {QStringLiteral("quantizationLevel"),
         live_probe.value(QStringLiteral("quantizationLevel"))},
        {QStringLiteral("probeAvailable"), true},
        {QStringLiteral("planning"),
         QVariantMap{
             {QStringLiteral("ok"), true},
             {QStringLiteral("decision"), live_plan.decision},
             {QStringLiteral("summary"), live_plan.plan_summary},
             {QStringLiteral("imageIncluded"), true},
             {QStringLiteral("fallbackUsed"), false},
             {QStringLiteral("elapsedMs"), live_plan_elapsed_ms},
             {QStringLiteral("transport"), live_plan_transport},
             {QStringLiteral("ollamaServer"),
              live_plan.prompt_audit.value(QStringLiteral("ollamaServer"))},
             {QStringLiteral("responseCharCount"),
              live_plan.prompt_audit.value(QStringLiteral("responseCharCount"))},
             {QStringLiteral("generationSchemaContract"),
              live_generation_schema_contract},
             {QStringLiteral("generationSchemaByteCount"),
              live_generation_schema_byte_count},
             {QStringLiteral("globalChangePolicy"),
              live_plan.edit_intent.value(QStringLiteral("globalChangePolicy"))},
             {QStringLiteral("globalChangePolicyCorrection"),
              live_scope_correction},
         }},
        {QStringLiteral("proofCritique"),
         QVariantMap{
             {QStringLiteral("ok"), true},
             {QStringLiteral("pass"), live_critique.pass},
             {QStringLiteral("needsUserReview"),
              live_critique.needs_user_review},
             {QStringLiteral("summary"), live_critique.summary},
             {QStringLiteral("tasteConfidence"),
              live_critique.taste_confidence},
             {QStringLiteral("imageIncluded"), true},
             {QStringLiteral("elapsedMs"), live_critique_elapsed_ms},
             {QStringLiteral("transport"),
              live_critique.prompt_audit
                  .value(QStringLiteral("transport"))},
             {QStringLiteral("ollamaServer"),
              live_critique.prompt_audit
                  .value(QStringLiteral("ollamaServer"))},
         }},
        {QStringLiteral("localOnly"), true},
        {QStringLiteral("cloudUsed"), false},
        {QStringLiteral("automaticCommitAllowed"), false},
        {QStringLiteral("photographerApprovalRequired"), true},
    };
    const auto receipt_path =
        qEnvironmentVariable("PHOTO_EDITOR_LIVE_OLLAMA_RECEIPT_PATH")
            .trimmed();
    if (!receipt_path.isEmpty()) {
      QFile receipt_file(receipt_path);
      if (!QDir().mkpath(QFileInfo(receipt_path).absolutePath()) ||
          !receipt_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
          receipt_file.write(
              QJsonDocument::fromVariant(live_receipt)
                  .toJson(QJsonDocument::Indented)) <= 0) {
        return fail("live Ollama smoke receipt should be writable");
      }
      receipt_file.close();
      std::cout << "live_ollama_receipt="
                << receipt_path.toStdString() << '\n';
    }
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
  }

  return EXIT_SUCCESS;
}
