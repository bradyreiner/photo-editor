#include <algorithm>
#include <cstdlib>
#include <iostream>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
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
  }

  void setChunkedResponse(const QString& path, const QJsonObject& response) {
    responses_.insert(path, response);
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
    writeResponse(socket, 200, responses_.value(path));
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
  QSet<QString> chunked_paths_;
  QVector<QJsonObject> requests_;
};

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("AgentLocalModelTest"));

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

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("temporary runtime directory should be created");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
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
      {QStringLiteral("decision"), QStringLiteral("recipe_patch")},
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
      {QStringLiteral("recipePatch"),
       QJsonObject{
           {QStringLiteral("globalTone"),
            QJsonObject{
                {QStringLiteral("exposure"), 0.12},
                {QStringLiteral("highlights"), -10.0},
                {QStringLiteral("shadows"), 8.0},
            }},
           {QStringLiteral("color"),
            QJsonObject{
                {QStringLiteral("temperature"), 4.0},
                {QStringLiteral("vibrance"), 5.0},
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
  request.current_recipe_json = QStringLiteral("{\"version\":1}");
  request.user_instructions = QStringLiteral("Use the image and make a polished warm edit.");
  request.strategy_phase = QStringLiteral("batch");
  request.memory_packet = QVariantMap{
      {QStringLiteral("phase"), QStringLiteral("batch")},
      {QStringLiteral("includedMemoryKinds"),
       QStringList{
           QStringLiteral("approved-sample-recipe"),
           QStringLiteral("cluster-summary"),
           QStringLiteral("style-constraints"),
      }},
      {QStringLiteral("rawVaultIncluded"), false},
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
  };
  const auto ollama_plan = photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto fake_request = fake_ollama_server_available
                                ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
                                : read_json_file(fake_ollama_request_path);
  const auto fake_messages = fake_request.value(QStringLiteral("messages")).toArray();
  const auto fake_format = fake_request.value(QStringLiteral("format")).toObject();
  const auto fake_required_fields = fake_format.value(QStringLiteral("required")).toArray();
  const auto fake_decision_values = fake_format.value(QStringLiteral("properties"))
                                        .toObject()
                                        .value(QStringLiteral("decision"))
                                        .toObject()
                                        .value(QStringLiteral("enum"))
                                        .toArray();
  const auto fake_user_message = fake_messages.size() >= 2 ? fake_messages.at(1).toObject() : QJsonObject{};
  const auto fake_user_prompt = fake_user_message.value(QStringLiteral("content")).toString();
  const auto fake_ollama_request_ok =
      fake_request.value(QStringLiteral("model")).toString() == QStringLiteral("qwen2.5vl:7b") &&
      !fake_request.value(QStringLiteral("stream")).toBool(true) &&
      !fake_request.value(QStringLiteral("think")).toBool(true) &&
      fake_request.value(QStringLiteral("keep_alive")).toString() == QStringLiteral("10m") &&
      fake_request.value(QStringLiteral("options")).toObject().value(QStringLiteral("temperature")).toDouble(-1.0) == 0.0 &&
      fake_request.value(QStringLiteral("options")).toObject().value(QStringLiteral("num_predict")).toInt() == 1024 &&
      fake_request.value(QStringLiteral("options")).toObject().value(QStringLiteral("num_ctx")).toInt() >= 12288 &&
      fake_request.value(QStringLiteral("options")).toObject().value(QStringLiteral("num_ctx")).toInt() <= 24576 &&
      fake_request.value(QStringLiteral("format")).isObject() &&
      fake_required_fields.contains(QStringLiteral("styleConstraintCitations")) &&
      fake_required_fields.contains(QStringLiteral("recipePatch")) &&
      fake_decision_values.contains(QStringLiteral("recipe_patch")) &&
      fake_user_prompt.contains(QStringLiteral("Strategy phase: batch")) &&
      fake_user_prompt.contains(QStringLiteral("Phase: batch")) &&
      fake_user_prompt.contains(QStringLiteral("Retrieved memory packet JSON")) &&
      fake_user_prompt.contains(QStringLiteral("approved-run-1")) &&
      fake_user_prompt.contains(QStringLiteral("Compact relevant examples JSON")) &&
      fake_user_prompt.contains(QStringLiteral("approved-sample-recipe")) &&
      fake_user_prompt.contains(QStringLiteral("Recipe patch schema JSON")) &&
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
  if (!ollama_plan.ok ||
      ollama_plan.decision != QStringLiteral("recipe_patch") ||
      ollama_plan.plan_summary != QStringLiteral("Fake Ollama warm proof edit") ||
      ollama_plan.prompt_audit.value(QStringLiteral("backend")).toString() != QStringLiteral("ollama") ||
      ollama_plan.prompt_audit.value(QStringLiteral("strategyPhase")).toString() != QStringLiteral("batch") ||
      !ollama_plan.prompt_audit.value(QStringLiteral("deterministicPlanning")).toBool() ||
      ollama_plan.prompt_audit.value(QStringLiteral("temperature")).toDouble() != 0.0 ||
      ollama_plan.prompt_audit.value(QStringLiteral("memoryPacketPhase")).toString() != QStringLiteral("batch") ||
      ollama_plan.prompt_audit.value(QStringLiteral("fewShotExampleCount")).toInt() != 1 ||
      ollama_plan.prompt_audit.value(QStringLiteral("rawMemoryVaultIncluded")).toBool() ||
      !ollama_plan.prompt_audit.value(QStringLiteral("userPrompt")).toString().contains(QStringLiteral("Recipe patch schema JSON")) ||
      !ollama_plan.prompt_audit.value(QStringLiteral("imageIncluded")).toBool() ||
      ollama_plan.prompt_audit.value(QStringLiteral("imageByteCount")).toInt() != fake_proof_bytes.size() ||
      ollama_plan.prompt_audit.value(QStringLiteral("imageSourceKind")).toString() != QStringLiteral("proof") ||
      ollama_plan.prompt_audit.value(QStringLiteral("imageSourcePath")).toString() != fake_proof_path ||
      !ollama_plan.prompt_audit.value(QStringLiteral("transport")).toMap().value(QStringLiteral("ok")).toBool() ||
      ollama_plan.prompt_audit.value(QStringLiteral("ollamaResponseSource")).toString() != QStringLiteral("message.thinking") ||
      !ollama_plan.prompt_audit.value(QStringLiteral("responseHasRecipePatch")).toBool() ||
      ollama_plan.prompt_audit.value(QStringLiteral("responseCitationCount")).toInt() != 2 ||
      ollama_plan.recipe_patch.value(QStringLiteral("globalTone")).toMap().value(QStringLiteral("exposure")).toDouble() != 0.12 ||
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

  request.model_id = QStringLiteral("missing-model");
  const auto missing_model_plan = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (missing_model_plan.ok || missing_model_plan.error_message.isEmpty()) {
    return fail("unsupported local model ids should be rejected");
  }

  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH");
  qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");

  return EXIT_SUCCESS;
}
