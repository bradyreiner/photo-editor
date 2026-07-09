#include "agent_local_model.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QRegularExpression>
#include <QSet>
#include <QTcpSocket>
#include <QUrl>

#include <algorithm>

#include "agent_edit_recipe_patch.h"

namespace photo_editor::desktop {
namespace {

constexpr auto kRulebookDeterministicModelId = "rulebook-deterministic";
constexpr auto kCloudModelOptInEnv = "PHOTO_EDITOR_ENABLE_CLOUD_MODELS";

QString configured_app_data_root() {
  const auto override_root = qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_DIR").trimmed();
  if (!override_root.isEmpty()) {
    return QDir::cleanPath(override_root);
  }
  const auto legacy_override_root = qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_ROOT").trimmed();
  if (!legacy_override_root.isEmpty()) {
    return QDir::cleanPath(legacy_override_root);
  }
  return QDir::current().absoluteFilePath(QStringLiteral(".photo-editor"));
}

QString model_manifest_path() {
  return QDir(configured_app_data_root()).filePath(QStringLiteral("agent-models/models.json"));
}

bool valid_model_id(const QString& id) {
  static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9._-]{2,80}$"));
  return pattern.match(id).hasMatch();
}

bool cloud_models_enabled() {
  const auto value = qEnvironmentVariable(kCloudModelOptInEnv).trimmed().toLower();
  return value == QStringLiteral("1") || value == QStringLiteral("true") ||
         value == QStringLiteral("yes") || value == QStringLiteral("on");
}

QString normalized_model_status(const QString& status, bool available_fallback) {
  const auto normalized = status.trimmed().toLower();
  if (normalized == QStringLiteral("available") ||
      normalized == QStringLiteral("loading") ||
      normalized == QStringLiteral("busy") ||
      normalized == QStringLiteral("failed") ||
      normalized == QStringLiteral("unavailable")) {
    return normalized;
  }
  return available_fallback ? QStringLiteral("available") : QStringLiteral("unavailable");
}

bool local_http_endpoint_valid(const QString& endpoint) {
  const QUrl url(endpoint.trimmed());
  return url.isValid() &&
         url.scheme() == QStringLiteral("http") &&
         (url.host() == QStringLiteral("127.0.0.1") || url.host() == QStringLiteral("localhost")) &&
         url.port() > 0;
}

QVariantMap failed_configured_model(const QString& id, const QString& reason) {
  return QVariantMap{
      {QStringLiteral("id"), id.isEmpty() ? QStringLiteral("invalid-configured-model") : id},
      {QStringLiteral("name"), id.isEmpty() ? QStringLiteral("Invalid configured model") : id},
      {QStringLiteral("backend"), QStringLiteral("local")},
      {QStringLiteral("status"), QStringLiteral("failed")},
      {QStringLiteral("available"), false},
      {QStringLiteral("statusReason"), reason},
      {QStringLiteral("configured"), true},
  };
}

QVariantList configured_agent_local_models(const QSet<QString>& existing_ids) {
  QFile file(model_manifest_path());
  if (!file.exists()) {
    return {};
  }
  if (!file.open(QIODevice::ReadOnly)) {
    return {failed_configured_model(
        QStringLiteral("configured-models"),
        QStringLiteral("Unable to read configured model manifest."))};
  }

  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError) {
    return {failed_configured_model(
        QStringLiteral("configured-models"),
        QStringLiteral("Configured model manifest is not valid JSON: %1").arg(parse_error.errorString()))};
  }

  QJsonArray model_values;
  if (document.isArray()) {
    model_values = document.array();
  } else if (document.isObject()) {
    model_values = document.object().value(QStringLiteral("models")).toArray();
  }
  if (model_values.isEmpty()) {
    return {};
  }

  QVariantList models;
  QSet<QString> seen_ids = existing_ids;
  const auto root = configured_app_data_root();
  for (const auto& model_value : model_values) {
    const auto object = model_value.toObject();
    const auto id = object.value(QStringLiteral("id")).toString().trimmed();
    if (!valid_model_id(id)) {
      models.append(failed_configured_model(
          id,
          QStringLiteral("Configured model id must contain only letters, numbers, dot, underscore, or dash.")));
      continue;
    }
    if (seen_ids.contains(id)) {
      models.append(failed_configured_model(
          id,
          QStringLiteral("Configured model id duplicates an existing model.")));
      continue;
    }
    seen_ids.insert(id);

    const auto backend =
        object.value(QStringLiteral("backend")).toString(QStringLiteral("local")).trimmed().toLower();
    auto model = QVariantMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), object.value(QStringLiteral("name")).toString(id)},
        {QStringLiteral("backend"), backend},
        {QStringLiteral("description"), object.value(QStringLiteral("description")).toString()},
        {QStringLiteral("configured"), true},
    };
    if (backend == QStringLiteral("ollama")) {
      const auto endpoint =
          object.value(QStringLiteral("endpoint")).toString(QStringLiteral("http://127.0.0.1:11434")).trimmed();
      const auto model_name = object.value(QStringLiteral("model")).toString().trimmed();
      model.insert(QStringLiteral("endpoint"), endpoint);
      model.insert(QStringLiteral("model"), model_name);
      model.insert(QStringLiteral("vision"), object.value(QStringLiteral("vision")).toBool(false));
      model.insert(QStringLiteral("requiresCloudOptIn"), false);
      if (model_name.isEmpty()) {
        model.insert(QStringLiteral("status"), QStringLiteral("failed"));
        model.insert(QStringLiteral("available"), false);
        model.insert(QStringLiteral("statusReason"), QStringLiteral("Ollama model manifests must include a model name."));
      } else if (!local_http_endpoint_valid(endpoint)) {
        model.insert(QStringLiteral("status"), QStringLiteral("failed"));
        model.insert(QStringLiteral("available"), false);
        model.insert(QStringLiteral("statusReason"), QStringLiteral("Ollama endpoint must be a localhost http URL."));
      } else {
        const auto manifest_available = object.value(QStringLiteral("available")).toBool(true);
        const auto status = normalized_model_status(
            object.value(QStringLiteral("status")).toString(),
            manifest_available);
        model.insert(QStringLiteral("status"), status);
        model.insert(QStringLiteral("available"), status == QStringLiteral("available"));
      }
      models.append(model);
      continue;
    }

    if (backend != QStringLiteral("local")) {
      const auto manifest_opt_in = object.value(QStringLiteral("allowCloud")).toBool(false);
      const auto environment_opt_in = cloud_models_enabled();
      model.insert(QStringLiteral("requiresCloudOptIn"), true);
      model.insert(QStringLiteral("cloudManifestOptIn"), manifest_opt_in);
      model.insert(QStringLiteral("cloudEnvironmentOptIn"), environment_opt_in);
      model.insert(QStringLiteral("available"), false);
      if (!manifest_opt_in) {
        model.insert(QStringLiteral("status"), QStringLiteral("failed"));
        model.insert(
            QStringLiteral("statusReason"),
            QStringLiteral("Cloud model manifests must set allowCloud: true before they can be considered."));
      } else if (!environment_opt_in) {
        model.insert(QStringLiteral("status"), QStringLiteral("failed"));
        model.insert(
            QStringLiteral("statusReason"),
            QStringLiteral("Set PHOTO_EDITOR_ENABLE_CLOUD_MODELS=1 to explicitly opt in to cloud models."));
      } else {
        model.insert(QStringLiteral("status"), QStringLiteral("unavailable"));
        model.insert(
            QStringLiteral("statusReason"),
            QStringLiteral("Cloud model execution is not implemented by the local agent runner."));
      }
      models.append(model);
      continue;
    }

    const auto raw_path = object.value(QStringLiteral("path")).toString().trimmed();
    auto path_exists = true;
    if (!raw_path.isEmpty()) {
      const auto path = QFileInfo(raw_path).isAbsolute() ? raw_path : QDir(root).filePath(raw_path);
      const QFileInfo info(path);
      path_exists = info.exists();
      model.insert(QStringLiteral("path"), info.absoluteFilePath());
    }
    const auto manifest_available = object.value(QStringLiteral("available")).toBool(path_exists);
    auto status = normalized_model_status(
        object.value(QStringLiteral("status")).toString(),
        manifest_available && path_exists);
    if (!path_exists) {
      status = QStringLiteral("failed");
      model.insert(QStringLiteral("statusReason"), QStringLiteral("Configured local model path does not exist."));
    }
    model.insert(QStringLiteral("status"), status);
    model.insert(QStringLiteral("available"), status == QStringLiteral("available"));
    models.append(model);
  }
  return models;
}

double number_constraint(
    const QVariantMap& style,
    const QString& key,
    double fallback,
    double minimum,
    double maximum) {
  const auto constraints = style.value(QStringLiteral("constraints")).toMap();
  auto ok = false;
  const auto value = constraints.value(key, fallback).toDouble(&ok);
  return std::clamp(ok ? value : fallback, minimum, maximum);
}

QVariantMap color_constraints(const QVariantMap& style) {
  return style.value(QStringLiteral("constraints"))
      .toMap()
      .value(QStringLiteral("color_constraints"))
      .toMap();
}

double number_color_constraint(
    const QVariantMap& style,
    const QString& key,
    double fallback,
    double minimum,
    double maximum) {
  const auto constraints = color_constraints(style);
  auto ok = false;
  const auto value = constraints.value(key, fallback).toDouble(&ok);
  return std::clamp(ok ? value : fallback, minimum, maximum);
}

QString string_color_constraint(
    const QVariantMap& style,
    const QString& key,
    const QString& fallback) {
  const auto value = color_constraints(style).value(key).toString().trimmed().toLower();
  return value.isEmpty() ? fallback : value;
}

bool bool_color_constraint(const QVariantMap& style, const QString& key, bool fallback) {
  const auto constraints = color_constraints(style);
  return constraints.contains(key) ? constraints.value(key).toBool() : fallback;
}

bool style_tool_enabled(const QVariantMap& style, const QString& key) {
  const auto value = style.value(QStringLiteral("allowedTools")).toMap().value(key);
  if (value.typeId() == QMetaType::Bool) {
    return value.toBool();
  }
  const auto text = value.toString().trimmed().toLower();
  return !text.isEmpty() && text != QStringLiteral("false") &&
         text != QStringLiteral("off") && text != QStringLiteral("none");
}

bool protected_hues_contains(const QVariantMap& style, const QString& hue) {
  const auto protected_hues =
      color_constraints(style).value(QStringLiteral("protected_hues")).toList();
  for (const auto& hue_value : protected_hues) {
    if (hue_value.toString().compare(hue, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}

bool text_contains(const QString& text, const QString& pattern) {
  return text.contains(pattern, Qt::CaseInsensitive);
}

QJsonObject http_post_json(
    const QString& endpoint,
    const QString& path,
    const QJsonObject& payload,
    int timeout_ms,
    QString* error_message) {
  QUrl url(endpoint.trimmed());
  if (!local_http_endpoint_valid(endpoint)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Ollama endpoint must be a localhost http URL.");
    }
    return {};
  }
  const auto fake_response_sequence = qEnvironmentVariable("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON");
  if (!fake_response_sequence.trimmed().isEmpty()) {
    static auto fake_sequence_index = 0;
    const auto request_path = qEnvironmentVariable("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH").trimmed();
    if (!request_path.isEmpty()) {
      QFile request_file(request_path);
      if (request_file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        request_file.write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
        request_file.write("\n");
      }
    }
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(fake_response_sequence.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isArray()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Fake Ollama response sequence JSON is invalid.");
      }
      return {};
    }
    const auto sequence = document.array();
    if (sequence.isEmpty()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Fake Ollama response sequence is empty.");
      }
      return {};
    }
    const auto index = std::min(fake_sequence_index, static_cast<int>(sequence.size()) - 1);
    ++fake_sequence_index;
    const auto response = sequence.at(index).toObject();
    if (response.isEmpty()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Fake Ollama sequence response is not an object.");
      }
      return {};
    }
    return response;
  }
  const auto fake_response = qEnvironmentVariable("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  if (!fake_response.trimmed().isEmpty()) {
    const auto request_path = qEnvironmentVariable("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH").trimmed();
    if (!request_path.isEmpty()) {
      QFile request_file(request_path);
      if (request_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        request_file.write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
      }
    }
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(fake_response.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Fake Ollama response JSON is invalid.");
      }
      return {};
    }
    return document.object();
  }
  auto request_path = path;
  if (!request_path.startsWith(QLatin1Char('/'))) {
    request_path.prepend(QLatin1Char('/'));
  }

  QTcpSocket socket;
  socket.connectToHost(url.host(), static_cast<quint16>(url.port()));
  if (!socket.waitForConnected(timeout_ms)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to connect to Ollama endpoint: %1").arg(socket.errorString());
    }
    return {};
  }

  const auto body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
  QByteArray request;
  request.append("POST ");
  request.append(request_path.toUtf8());
  request.append(" HTTP/1.1\r\nHost: ");
  request.append(url.host().toUtf8());
  request.append(':');
  request.append(QByteArray::number(url.port()));
  request.append("\r\nOrigin: http://127.0.0.1\r\nAccept: application/json\r\nContent-Type: application/json\r\nContent-Length: ");
  request.append(QByteArray::number(body.size()));
  request.append("\r\n\r\n");
  request.append(body);
  socket.write(request);
  socket.flush();
  if (!socket.waitForBytesWritten(timeout_ms)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Timed out writing Ollama request.");
    }
    return {};
  }

  QByteArray response;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    if (QCoreApplication::instance() != nullptr) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    if (socket.waitForReadyRead(10)) {
      response.append(socket.readAll());
      const auto header_end = response.indexOf("\r\n\r\n");
      if (header_end < 0) {
        continue;
      }
      const auto header = response.left(header_end);
      auto content_length = -1;
      for (const auto& line_bytes : header.split('\n')) {
        const auto line = QString::fromLatin1(line_bytes).trimmed();
        if (line.toLower().startsWith(QStringLiteral("content-length:"))) {
          auto ok = false;
          content_length = line.mid(QStringLiteral("content-length:").size()).trimmed().toInt(&ok);
          if (!ok) {
            content_length = -1;
          }
        }
      }
      if (content_length >= 0 && response.size() >= header_end + 4 + content_length) {
        break;
      }
    }
  }

  const auto header_end = response.indexOf("\r\n\r\n");
  if (header_end < 0) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Ollama returned an incomplete HTTP response.");
    }
    return {};
  }
  const auto status_line = QString::fromLatin1(response.left(response.indexOf('\n'))).trimmed();
  const auto status_parts = status_line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  const auto status_code = status_parts.size() >= 2 ? status_parts.at(1).toInt() : 0;
  if (status_code < 200 || status_code >= 300) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Ollama returned HTTP %1.").arg(status_code);
    }
    return {};
  }
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(response.mid(header_end + 4), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Ollama returned invalid JSON.");
    }
    return {};
  }
  return document.object();
}

double bounded(double value, double limit) {
  return std::clamp(value, -std::abs(limit), std::abs(limit));
}

void append_style_citation(
    QVariantList* citations,
    const QString& section,
    const QString& key,
    const QVariant& value,
    const QString& reason) {
  if (citations == nullptr || !value.isValid() || value.isNull()) {
    return;
  }
  citations->append(QVariantMap{
      {QStringLiteral("field"), QStringLiteral("%1.%2").arg(section, key)},
      {QStringLiteral("section"), section},
      {QStringLiteral("key"), key},
      {QStringLiteral("value"), value},
      {QStringLiteral("reason"), reason},
  });
}

struct ModelImagePayload final {
  QString base64;
  QString source_path;
  QString source_kind;
};

ModelImagePayload image_payload_for_asset(const QVariantMap& asset) {
  const QList<QPair<QString, QString>> candidate_keys{
      {QStringLiteral("proofImagePath"), QStringLiteral("proof")},
      {QStringLiteral("currentProofPath"), QStringLiteral("current-proof")},
      {QStringLiteral("afterProofPath"), QStringLiteral("after-proof")},
      {QStringLiteral("beforeProofPath"), QStringLiteral("before-proof")},
      {QStringLiteral("filePath"), QStringLiteral("source-file")},
  };
  for (const auto& candidate : candidate_keys) {
    const auto file_path = asset.value(candidate.first).toString();
    if (file_path.trimmed().isEmpty()) {
      continue;
    }
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly)) {
      continue;
    }
    return ModelImagePayload{
        QString::fromLatin1(file.readAll().toBase64()),
        file_path,
        candidate.second,
    };
  }
  return {};
}

QJsonObject ollama_response_payload_schema() {
  return QJsonObject{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("properties"),
       QJsonObject{
           {QStringLiteral("decision"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
           {QStringLiteral("summary"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
           {QStringLiteral("styleConstraintCitations"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
           {QStringLiteral("recipePatch"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
           {QStringLiteral("toolCalls"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
           {QStringLiteral("reason"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
           {QStringLiteral("userQuestions"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
           {QStringLiteral("warnings"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
       }},
      {QStringLiteral("required"),
       QJsonArray{
           QStringLiteral("decision"),
           QStringLiteral("summary"),
       }},
  };
}

QJsonObject ollama_proof_critique_payload_schema() {
  return QJsonObject{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("properties"),
       QJsonObject{
           {QStringLiteral("pass"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
           {QStringLiteral("summary"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
           {QStringLiteral("issues"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
           {QStringLiteral("repairHints"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
           {QStringLiteral("needsUserReview"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
           {QStringLiteral("tasteConfidence"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
           {QStringLiteral("warnings"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
       }},
      {QStringLiteral("required"),
       QJsonArray{
           QStringLiteral("pass"),
           QStringLiteral("summary"),
           QStringLiteral("issues"),
           QStringLiteral("repairHints"),
           QStringLiteral("needsUserReview"),
           QStringLiteral("tasteConfidence"),
       }},
  };
}

QString normalized_strategy_phase(const QString& phase) {
  const auto normalized = phase.trimmed().toLower();
  if (normalized == QStringLiteral("setup") ||
      normalized == QStringLiteral("sample") ||
      normalized == QStringLiteral("revision") ||
      normalized == QStringLiteral("batch") ||
      normalized == QStringLiteral("outlier")) {
    return normalized;
  }
  return QStringLiteral("sample");
}

QString strategy_prompt_for_phase(const QString& phase) {
  const auto normalized = normalized_strategy_phase(phase);
  if (normalized == QStringLiteral("revision")) {
    return QStringLiteral(
        "Phase: revision. Use the current validation feedback and user correction to produce the smallest replacement patch that fixes the sample. Do not broaden scope.");
  }
  if (normalized == QStringLiteral("batch")) {
    return QStringLiteral(
        "Phase: batch. Preserve the approved sample direction, adapt only for compact cluster differences, and keep edits reproducible across the set.");
  }
  if (normalized == QStringLiteral("outlier")) {
    return QStringLiteral(
        "Phase: outlier. Fix one flagged photo using the issue report. Prefer the least invasive change and preserve the approved batch look.");
  }
  if (normalized == QStringLiteral("setup")) {
    return QStringLiteral(
        "Phase: setup. Report readiness only. Do not propose aesthetic edits.");
  }
  return QStringLiteral(
      "Phase: sample. Create a representative first edit from the active style, proof image, current recipe, and compact memory. Prefer a safe sample over a dramatic batch-wide change.");
}

QVariantList compact_few_shot_examples_from_memory(const QVariantMap& memory_packet) {
  QVariantList examples;
  const auto phase = memory_packet.value(QStringLiteral("phase")).toString();
  const auto approved_sample =
      memory_packet.value(QStringLiteral("approvedSampleRecipeCard")).toMap();
  if ((phase == QStringLiteral("batch") || phase == QStringLiteral("outlier")) &&
      approved_sample.value(QStringLiteral("available")).toBool() &&
      approved_sample.value(QStringLiteral("validationOk")).toBool()) {
    examples.append(QVariantMap{
        {QStringLiteral("kind"), QStringLiteral("approved-sample-recipe")},
        {QStringLiteral("runId"), approved_sample.value(QStringLiteral("runId"))},
        {QStringLiteral("scope"), approved_sample.value(QStringLiteral("scope"))},
        {QStringLiteral("recipePatch"), approved_sample.value(QStringLiteral("recipePatch"))},
    });
  }
  const auto latest_issue = memory_packet.value(QStringLiteral("latestIssueCard")).toMap();
  if ((phase == QStringLiteral("revision") || phase == QStringLiteral("outlier")) &&
      latest_issue.value(QStringLiteral("available")).toBool()) {
    examples.append(QVariantMap{
        {QStringLiteral("kind"), QStringLiteral("prior-issue-fix")},
        {QStringLiteral("runId"), latest_issue.value(QStringLiteral("runId"))},
        {QStringLiteral("summary"), latest_issue.value(QStringLiteral("summary"))},
    });
  }
  while (examples.size() > 2) {
    examples.removeLast();
  }
  return examples;
}

QString ollama_agent_prompt(const AgentLocalModelPlanRequest& request) {
  const auto style_json =
      QString::fromUtf8(QJsonDocument::fromVariant(request.style).toJson(QJsonDocument::Compact));
  const auto schema_json =
      QString::fromUtf8(QJsonDocument::fromVariant(agent_recipe_patch_tool_schema()).toJson(QJsonDocument::Compact));
  const auto available_tools_json =
      QString::fromUtf8(QJsonDocument::fromVariant(request.available_tool_schemas).toJson(QJsonDocument::Compact));
  const auto prior_tool_results_json =
      QString::fromUtf8(QJsonDocument::fromVariant(request.prior_tool_results).toJson(QJsonDocument::Compact));
  const auto memory_packet_json =
      QString::fromUtf8(QJsonDocument::fromVariant(request.memory_packet).toJson(QJsonDocument::Compact));
  const auto examples = request.few_shot_examples.isEmpty()
                            ? compact_few_shot_examples_from_memory(request.memory_packet)
                            : request.few_shot_examples;
  const auto few_shot_examples_json =
      QString::fromUtf8(QJsonDocument::fromVariant(examples).toJson(QJsonDocument::Compact));
  const auto strategy_phase = normalized_strategy_phase(request.strategy_phase);
  const auto strategy_prompt = strategy_prompt_for_phase(strategy_phase);
  return QStringLiteral(
      "You are a local photo-editing planning agent. Return JSON only. "
      "Use the phase strategy exactly; do not collapse sample, revision, batch, and outlier behavior into one generic plan. "
      "Do not edit files directly and do not invent tools. "
      "Use only the listed tools when you need more context. "
      "When current-proof validation evidence is available in prior tool results, use imageStats.histogram and imageStats.vectorscope to decide tone, white balance, saturation, skin, mask, and layer strategy. "
      "If you already have enough context, return decision=recipe_patch with a recipePatch that follows the style rulebook and validation-backed recipe schema. "
      "If you need more context and tool calls are allowed, return decision=tool_call_plan with toolCalls. "
      "If user intent is ambiguous, return decision=needs_user_input with userQuestions. "
      "If the edit cannot be completed safely under the rulebook, return decision=cannot_complete_safely with reason. "
      "After tool results are provided, return decision=recipe_patch. "
      "Include styleConstraintCitations with active style fields when returning a recipe patch.\n"
      "Strategy phase: %1\n"
      "Phase strategy:\n%2\n"
      "User instructions:\n%3\n"
      "Current recipe JSON:\n%4\n"
      "Style rulebook JSON:\n%5\n"
      "Retrieved memory packet JSON:\n%6\n"
      "Compact relevant examples JSON:\n%7\n"
      "Recipe patch schema JSON:\n%8\n"
      "Tool-call planning allowed: %9\n"
      "Available MCP tool schemas JSON:\n%10\n"
      "Prior MCP tool results JSON:\n%11")
      .arg(
          strategy_phase,
          strategy_prompt,
          request.user_instructions,
          request.current_recipe_json,
          style_json,
          memory_packet_json,
          few_shot_examples_json,
          schema_json,
          request.allow_tool_call_plan ? QStringLiteral("true") : QStringLiteral("false"),
          available_tools_json,
      prior_tool_results_json);
}

QString ollama_proof_critique_prompt(const AgentLocalModelProofCritiqueRequest& request) {
  const auto style_json =
      QString::fromUtf8(QJsonDocument::fromVariant(request.style).toJson(QJsonDocument::Compact));
  const auto validation_json =
      QString::fromUtf8(QJsonDocument::fromVariant(request.validation_report).toJson(QJsonDocument::Compact));
  const auto attempts_json =
      QString::fromUtf8(QJsonDocument::fromVariant(request.previous_attempts).toJson(QJsonDocument::Compact));
  return QStringLiteral(
      "You are a local photo-editing proof evaluator. Return JSON only. "
      "Do not propose a recipePatch and do not mutate files. "
      "Evaluate whether the rendered after proof satisfies the user instructions, active style rulebook, and deterministic validation report. "
      "Use validationReport.imageStats.histogram like a histogram and validationReport.imageStats.vectorscope like a vectorscope; treat clipping, compressed tonal range, channel cast, hue imbalance, and skin drift as reasons to fail or ask for repair. "
      "Use pass=false for unresolved clipping, severe color cast, implausible skin, unsafe retouch, broken masks, or obvious style mismatch. "
      "Use repairHints as concise guidance for a later recipe-patch repair turn; never include raw image data or direct recipe JSON.\n"
      "User instructions:\n%1\n"
      "Style rulebook JSON:\n%2\n"
      "Validation report JSON:\n%3\n"
      "Previous attempts JSON:\n%4")
      .arg(
          request.user_instructions,
          style_json,
          validation_json,
          attempts_json);
}

AgentLocalModelPlanResult plan_ollama_recipe_patch(
    const AgentLocalModelPlanRequest& request,
    const QVariantMap& model) {
  AgentLocalModelPlanResult result;
  result.model_id = request.model_id.trimmed();
  if (request.style.isEmpty()) {
    result.error_message = QStringLiteral("A valid style is required before planning an agent edit.");
    return result;
  }
  const auto endpoint = model.value(QStringLiteral("endpoint")).toString();
  const auto model_name = model.value(QStringLiteral("model")).toString();
  const auto prompt = ollama_agent_prompt(request);
  const auto strategy_phase = normalized_strategy_phase(request.strategy_phase);
  const auto strategy_prompt = strategy_prompt_for_phase(strategy_phase);
  const auto few_shot_examples = request.few_shot_examples.isEmpty()
                                     ? compact_few_shot_examples_from_memory(request.memory_packet)
                                     : request.few_shot_examples;
  const auto system_prompt =
      QStringLiteral("Return one compact JSON object with summary, styleConstraintCitations, recipePatch, and optional warnings.");
  QJsonObject user_message{
      {QStringLiteral("role"), QStringLiteral("user")},
      {QStringLiteral("content"), prompt},
  };
  const auto image_payload = image_payload_for_asset(request.asset);
  const auto image_included = !image_payload.base64.isEmpty() && model.value(QStringLiteral("vision")).toBool();
  if (image_included) {
    user_message.insert(QStringLiteral("images"), QJsonArray{image_payload.base64});
  }
  const QJsonObject payload{
      {QStringLiteral("model"), model_name},
      {QStringLiteral("stream"), false},
      {QStringLiteral("format"), ollama_response_payload_schema()},
      {QStringLiteral("messages"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("role"), QStringLiteral("system")},
               {QStringLiteral("content"), system_prompt},
           },
           user_message,
       }},
      {QStringLiteral("options"), QJsonObject{{QStringLiteral("temperature"), 0.0}}},
  };
  result.prompt_audit = QVariantMap{
      {QStringLiteral("backend"), QStringLiteral("ollama")},
      {QStringLiteral("endpoint"), endpoint},
      {QStringLiteral("model"), model_name},
      {QStringLiteral("schemaMode"), QStringLiteral("json-schema")},
      {QStringLiteral("strategyPhase"), strategy_phase},
      {QStringLiteral("strategyPrompt"), strategy_prompt},
      {QStringLiteral("deterministicPlanning"), true},
      {QStringLiteral("temperature"), 0.0},
      {QStringLiteral("systemPrompt"), system_prompt},
      {QStringLiteral("userPrompt"), prompt},
      {QStringLiteral("userPromptCharCount"), prompt.size()},
      {QStringLiteral("availableToolCount"), request.available_tool_schemas.size()},
      {QStringLiteral("priorToolResultCount"), request.prior_tool_results.size()},
      {QStringLiteral("toolCallPlanningAllowed"), request.allow_tool_call_plan},
      {QStringLiteral("memoryPacketPhase"), request.memory_packet.value(QStringLiteral("phase"))},
      {QStringLiteral("includedMemoryKinds"),
       request.memory_packet.value(QStringLiteral("includedMemoryKinds"))},
      {QStringLiteral("rawMemoryVaultIncluded"),
       request.memory_packet.value(QStringLiteral("rawVaultIncluded")).toBool()},
      {QStringLiteral("fewShotExampleCount"), few_shot_examples.size()},
      {QStringLiteral("imageIncluded"), image_included},
      {QStringLiteral("imageByteCount"), QByteArray::fromBase64(image_payload.base64.toLatin1()).size()},
      {QStringLiteral("imageSourceKind"), image_payload.source_kind},
      {QStringLiteral("imageSourcePath"), image_payload.source_path},
      {QStringLiteral("responseSchema"), ollama_response_payload_schema().toVariantMap()},
  };

  QString error_message;
  const auto response = http_post_json(endpoint, QStringLiteral("/api/chat"), payload, 30000, &error_message);
  if (response.isEmpty()) {
    result.error_message = error_message.isEmpty()
                               ? QStringLiteral("Ollama did not return a response.")
                               : error_message;
    return result;
  }
  auto content = response.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
  if (content.trimmed().isEmpty()) {
    content = response.value(QStringLiteral("response")).toString();
  }
  QJsonParseError parse_error;
  const auto content_document = QJsonDocument::fromJson(content.toUtf8(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !content_document.isObject()) {
    result.error_message = QStringLiteral("Ollama response content was not valid JSON.");
    return result;
  }
  const auto content_object = content_document.object();
  result.decision = content_object.value(QStringLiteral("decision")).toString(QStringLiteral("recipe_patch")).trimmed();
  result.plan_summary = content_object.value(QStringLiteral("summary")).toString();
  result.style_constraint_citations =
      content_object.value(QStringLiteral("styleConstraintCitations")).toArray().toVariantList();
  result.recipe_patch =
      content_object.value(QStringLiteral("recipePatch")).toObject().toVariantMap();
  result.tool_call_plan =
      content_object.value(QStringLiteral("toolCalls")).toArray().toVariantList();
  result.safe_stop_reason = content_object.value(QStringLiteral("reason")).toString();
  result.user_questions =
      content_object.value(QStringLiteral("userQuestions")).toArray().toVariantList();
  const auto warning_values = content_object.value(QStringLiteral("warnings")).toArray();
  for (const auto& warning : warning_values) {
    result.warnings.append(warning.toString());
  }
  if (result.plan_summary.trimmed().isEmpty()) {
    result.error_message = QStringLiteral("Ollama response must include a summary.");
    return result;
  }
  if (result.decision == QStringLiteral("tool_call_plan")) {
    if (!request.allow_tool_call_plan) {
      result.error_message = QStringLiteral("Ollama requested tool calls when tool-call planning was not allowed.");
      return result;
    }
    if (result.tool_call_plan.isEmpty()) {
      result.error_message = QStringLiteral("Ollama tool_call_plan response must include toolCalls.");
      return result;
    }
    result.ok = true;
    return result;
  }
  if (result.decision == QStringLiteral("needs_user_input")) {
    if (result.user_questions.isEmpty()) {
      result.error_message = QStringLiteral("Ollama needs_user_input response must include userQuestions.");
      return result;
    }
    result.ok = true;
    return result;
  }
  if (result.decision == QStringLiteral("cannot_complete_safely")) {
    if (result.safe_stop_reason.trimmed().isEmpty()) {
      result.error_message = QStringLiteral("Ollama cannot_complete_safely response must include a reason.");
      return result;
    }
    result.ok = true;
    return result;
  }
  if (result.decision.isEmpty() || result.decision == QStringLiteral("recipe_patch")) {
    result.decision = QStringLiteral("recipe_patch");
  }
  if (result.decision != QStringLiteral("recipe_patch")) {
    result.error_message = QStringLiteral("Unsupported Ollama planning decision: %1").arg(result.decision);
    return result;
  }
  if (result.style_constraint_citations.isEmpty() || result.recipe_patch.isEmpty()) {
    result.error_message = QStringLiteral("Ollama recipe_patch response must include style citations and a recipePatch.");
    return result;
  }
  result.ok = true;
  return result;
}

QVariantMap model_metadata_by_id(const QString& id) {
  const auto models = available_agent_local_models();
  for (const auto& model_value : models) {
    const auto model = model_value.toMap();
    if (model.value(QStringLiteral("id")).toString() == id) {
      return model;
    }
  }
  return {};
}

void append_present_style_citation(
    QVariantList* citations,
    const QVariantMap& values,
    const QString& section,
    const QString& key,
    const QString& reason) {
  if (!values.contains(key)) {
    return;
  }
  append_style_citation(citations, section, key, values.value(key), reason);
}

QVariantList cited_style_constraints(const QVariantMap& style) {
  QVariantList citations;
  const auto constraints = style.value(QStringLiteral("constraints")).toMap();
  append_present_style_citation(
      &citations,
      constraints,
      QStringLiteral("constraints"),
      QStringLiteral("max_exposure_shift"),
      QStringLiteral("Bounds generated exposure changes before the recipe patch is proposed."));
  append_present_style_citation(
      &citations,
      constraints,
      QStringLiteral("constraints"),
      QStringLiteral("max_temperature_shift"),
      QStringLiteral("Bounds generated white balance warmth before the recipe patch is proposed."));
  append_present_style_citation(
      &citations,
      constraints,
      QStringLiteral("constraints"),
      QStringLiteral("max_saturation_shift"),
      QStringLiteral("Bounds generated vibrance and saturation changes before the recipe patch is proposed."));
  append_present_style_citation(
      &citations,
      constraints,
      QStringLiteral("constraints"),
      QStringLiteral("avoid_clipped_highlights"),
      QStringLiteral("Keeps highlight recovery active in the generated tone plan."));
  append_present_style_citation(
      &citations,
      constraints,
      QStringLiteral("constraints"),
      QStringLiteral("avoid_crushed_blacks"),
      QStringLiteral("Keeps black and shadow changes conservative in the generated tone plan."));
  append_present_style_citation(
      &citations,
      constraints,
      QStringLiteral("constraints"),
      QStringLiteral("preserve_skin_texture"),
      QStringLiteral("Keeps detail edits restrained when the style protects skin texture."));
  const auto color_constraints =
      constraints.value(QStringLiteral("color_constraints")).toMap();
  append_present_style_citation(
      &citations,
      color_constraints,
      QStringLiteral("constraints.color_constraints"),
      QStringLiteral("preferred_temperature_bias"),
      QStringLiteral("Steers generated white-balance direction for color-specific style behavior."));
  append_present_style_citation(
      &citations,
      color_constraints,
      QStringLiteral("constraints.color_constraints"),
      QStringLiteral("protected_hues"),
      QStringLiteral("Names hue families the planner should protect from aggressive channel edits."));
  append_present_style_citation(
      &citations,
      color_constraints,
      QStringLiteral("constraints.color_constraints"),
      QStringLiteral("max_hsl_channel_shift"),
      QStringLiteral("Bounds generated per-channel HSL mixer adjustments."));
  append_present_style_citation(
      &citations,
      color_constraints,
      QStringLiteral("constraints.color_constraints"),
      QStringLiteral("max_color_grading_saturation"),
      QStringLiteral("Bounds generated color-grading saturation controls."));
  append_present_style_citation(
      &citations,
      color_constraints,
      QStringLiteral("constraints.color_constraints"),
      QStringLiteral("allow_harmony"),
      QStringLiteral("Controls whether the planner may enable color harmony."));

  const auto validation = style.value(QStringLiteral("validation")).toMap();
  append_present_style_citation(
      &citations,
      validation,
      QStringLiteral("validation"),
      QStringLiteral("highlight_clip_limit_percent"),
      QStringLiteral("Sets the highlight clipping threshold checked after proof rendering."));
  append_present_style_citation(
      &citations,
      validation,
      QStringLiteral("validation"),
      QStringLiteral("shadow_clip_limit_percent"),
      QStringLiteral("Sets the shadow clipping threshold checked after proof rendering."));
  append_present_style_citation(
      &citations,
      validation,
      QStringLiteral("validation"),
      QStringLiteral("require_before_after_proof"),
      QStringLiteral("Requires proof artifacts before the draft is ready for review."));
  append_present_style_citation(
      &citations,
      validation,
      QStringLiteral("validation"),
      QStringLiteral("require_skin_tone_check"),
      QStringLiteral("Requires skin-tone validation before the draft is ready for review."));

  const auto allowed_tools = style.value(QStringLiteral("allowedTools")).toMap();
  append_present_style_citation(
      &citations,
      allowed_tools,
      QStringLiteral("allowedTools"),
      QStringLiteral("crop"),
      QStringLiteral("Limits whether the planner may use crop and transform tools."));
  append_present_style_citation(
      &citations,
      allowed_tools,
      QStringLiteral("allowedTools"),
      QStringLiteral("retouch"),
      QStringLiteral("Limits whether the planner may use retouch tools."));
  append_present_style_citation(
      &citations,
      allowed_tools,
      QStringLiteral("allowedTools"),
      QStringLiteral("layers_luts"),
      QStringLiteral("Limits whether the planner may use layer and LUT tools."));
  append_present_style_citation(
      &citations,
      allowed_tools,
      QStringLiteral("allowedTools"),
      QStringLiteral("export"),
      QStringLiteral("Limits whether the planner may queue export operations."));
  return citations;
}

}  // namespace

QVariantList available_agent_local_models() {
  QVariantList models;
  QVariantMap deterministic;
  deterministic.insert(QStringLiteral("id"), QString::fromLatin1(kRulebookDeterministicModelId));
  deterministic.insert(QStringLiteral("name"), QStringLiteral("Rulebook deterministic"));
  deterministic.insert(QStringLiteral("backend"), QStringLiteral("local"));
  deterministic.insert(QStringLiteral("status"), QStringLiteral("available"));
  deterministic.insert(QStringLiteral("available"), true);
  deterministic.insert(QStringLiteral("configured"), false);
  deterministic.insert(
      QStringLiteral("description"),
      QStringLiteral("Deterministic local planner used for repeatable agent runs and tests."));
  models.append(deterministic);
  models.append(configured_agent_local_models({QString::fromLatin1(kRulebookDeterministicModelId)}));
  return models;
}

QString default_agent_local_model_id() {
  return QString::fromLatin1(kRulebookDeterministicModelId);
}

QVariantMap probe_agent_local_model(const QString& model_id) {
  const auto resolved_model_id = model_id.trimmed().isEmpty() ? default_agent_local_model_id() : model_id.trimmed();
  const auto model = model_metadata_by_id(resolved_model_id);
  if (model.isEmpty()) {
    return QVariantMap{
        {QStringLiteral("ok"), false},
        {QStringLiteral("mode"), QStringLiteral("model-probe")},
        {QStringLiteral("modelId"), resolved_model_id},
        {QStringLiteral("available"), false},
        {QStringLiteral("status"), QStringLiteral("failed")},
        {QStringLiteral("statusReason"), QStringLiteral("Unsupported local model: %1").arg(resolved_model_id)},
    };
  }

  QVariantMap report{
      {QStringLiteral("ok"), true},
      {QStringLiteral("mode"), QStringLiteral("model-probe")},
      {QStringLiteral("modelId"), resolved_model_id},
      {QStringLiteral("backend"), model.value(QStringLiteral("backend"))},
      {QStringLiteral("configured"), model.value(QStringLiteral("configured"))},
      {QStringLiteral("available"), model.value(QStringLiteral("available"))},
      {QStringLiteral("status"), model.value(QStringLiteral("status"))},
  };
  const auto status_reason = model.value(QStringLiteral("statusReason")).toString();
  if (!status_reason.isEmpty()) {
    report.insert(QStringLiteral("statusReason"), status_reason);
  }

  if (model.value(QStringLiteral("backend")).toString() != QStringLiteral("ollama")) {
    return report;
  }

  const auto endpoint = model.value(QStringLiteral("endpoint")).toString().trimmed();
  const auto model_name = model.value(QStringLiteral("model")).toString().trimmed();
  report.insert(QStringLiteral("endpoint"), endpoint);
  report.insert(QStringLiteral("ollamaModel"), model_name);
  if (model_name.isEmpty()) {
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("available"), false);
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("statusReason"), QStringLiteral("Ollama model name is missing."));
    return report;
  }

  QString error_message;
  const auto response = http_post_json(
      endpoint,
      QStringLiteral("/api/show"),
      QJsonObject{{QStringLiteral("model"), model_name}},
      5000,
      &error_message);
  if (response.isEmpty()) {
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("available"), false);
    report.insert(QStringLiteral("status"), QStringLiteral("unavailable"));
    report.insert(
        QStringLiteral("statusReason"),
        error_message.isEmpty()
            ? QStringLiteral("Ollama model probe did not return a response.")
            : error_message);
    return report;
  }

  report.insert(QStringLiteral("available"), true);
  report.insert(QStringLiteral("status"), QStringLiteral("available"));
  report.insert(QStringLiteral("probeResponse"), response.toVariantMap());
  const auto details = response.value(QStringLiteral("details")).toObject();
  if (!details.isEmpty()) {
    report.insert(QStringLiteral("modelFamily"), details.value(QStringLiteral("family")).toString());
    report.insert(QStringLiteral("parameterSize"), details.value(QStringLiteral("parameter_size")).toString());
    report.insert(QStringLiteral("quantizationLevel"), details.value(QStringLiteral("quantization_level")).toString());
  }
  return report;
}

AgentLocalModelProofCritiqueResult critique_agent_rendered_proof(
    const AgentLocalModelProofCritiqueRequest& request) {
  AgentLocalModelProofCritiqueResult result;
  result.model_id = request.model_id.trimmed().isEmpty() ? default_agent_local_model_id()
                                                         : request.model_id.trimmed();
  const auto model = model_metadata_by_id(result.model_id);
  if (model.isEmpty()) {
    result.error_message = QStringLiteral("Unsupported local model: %1").arg(result.model_id);
    return result;
  }
  if (model.value(QStringLiteral("backend")).toString() != QStringLiteral("ollama")) {
    result.error_message = QStringLiteral("Proof critique requires an Ollama vision model.");
    return result;
  }
  if (!model.value(QStringLiteral("available")).toBool()) {
    result.error_message =
        model.value(QStringLiteral("statusReason")).toString().trimmed().isEmpty()
            ? QStringLiteral("Ollama model is not available: %1").arg(result.model_id)
            : model.value(QStringLiteral("statusReason")).toString();
    return result;
  }
  if (!model.value(QStringLiteral("vision")).toBool()) {
    result.error_message = QStringLiteral("Proof critique requires a vision-capable model.");
    return result;
  }
  if (request.style.isEmpty()) {
    result.error_message = QStringLiteral("A valid style is required before critiquing an agent proof.");
    return result;
  }

  const auto endpoint = model.value(QStringLiteral("endpoint")).toString();
  const auto model_name = model.value(QStringLiteral("model")).toString();
  const auto prompt = ollama_proof_critique_prompt(request);
  const auto system_prompt =
      QStringLiteral("Return one compact JSON object with pass, summary, issues, repairHints, needsUserReview, tasteConfidence, and optional warnings.");
  QJsonObject user_message{
      {QStringLiteral("role"), QStringLiteral("user")},
      {QStringLiteral("content"), prompt},
  };
  const auto image_payload = image_payload_for_asset(request.asset);
  const auto image_included = !image_payload.base64.isEmpty();
  if (image_included) {
    user_message.insert(QStringLiteral("images"), QJsonArray{image_payload.base64});
  }
  const QJsonObject payload{
      {QStringLiteral("model"), model_name},
      {QStringLiteral("stream"), false},
      {QStringLiteral("format"), ollama_proof_critique_payload_schema()},
      {QStringLiteral("messages"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("role"), QStringLiteral("system")},
               {QStringLiteral("content"), system_prompt},
           },
           user_message,
       }},
      {QStringLiteral("options"), QJsonObject{{QStringLiteral("temperature"), 0.0}}},
  };
  result.prompt_audit = QVariantMap{
      {QStringLiteral("backend"), QStringLiteral("ollama")},
      {QStringLiteral("endpoint"), endpoint},
      {QStringLiteral("model"), model_name},
      {QStringLiteral("schemaMode"), QStringLiteral("proof-critique-json-schema")},
      {QStringLiteral("deterministicPlanning"), true},
      {QStringLiteral("temperature"), 0.0},
      {QStringLiteral("systemPrompt"), system_prompt},
      {QStringLiteral("userPrompt"), prompt},
      {QStringLiteral("userPromptCharCount"), prompt.size()},
      {QStringLiteral("previousAttemptCount"), request.previous_attempts.size()},
      {QStringLiteral("imageIncluded"), image_included},
      {QStringLiteral("imageByteCount"), QByteArray::fromBase64(image_payload.base64.toLatin1()).size()},
      {QStringLiteral("imageSourceKind"), image_payload.source_kind},
      {QStringLiteral("imageSourcePath"), image_payload.source_path},
      {QStringLiteral("responseSchema"), ollama_proof_critique_payload_schema().toVariantMap()},
  };
  if (!image_included) {
    result.error_message = QStringLiteral("No rendered proof image was available for model critique.");
    return result;
  }

  QString error_message;
  const auto response = http_post_json(endpoint, QStringLiteral("/api/chat"), payload, 30000, &error_message);
  if (response.isEmpty()) {
    result.error_message = error_message.isEmpty()
                               ? QStringLiteral("Ollama proof critique did not return a response.")
                               : error_message;
    return result;
  }
  auto content = response.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
  if (content.trimmed().isEmpty()) {
    content = response.value(QStringLiteral("response")).toString();
  }
  QJsonParseError parse_error;
  const auto content_document = QJsonDocument::fromJson(content.toUtf8(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !content_document.isObject()) {
    result.error_message = QStringLiteral("Ollama proof critique content was not valid JSON.");
    return result;
  }
  const auto content_object = content_document.object();
  result.pass = content_object.value(QStringLiteral("pass")).toBool(false);
  result.summary = content_object.value(QStringLiteral("summary")).toString();
  result.issues = content_object.value(QStringLiteral("issues")).toArray().toVariantList();
  result.repair_hints = content_object.value(QStringLiteral("repairHints")).toArray().toVariantList();
  result.needs_user_review = content_object.value(QStringLiteral("needsUserReview")).toBool(!result.pass);
  result.taste_confidence = content_object.value(QStringLiteral("tasteConfidence")).toString();
  const auto warning_values = content_object.value(QStringLiteral("warnings")).toArray();
  for (const auto& warning : warning_values) {
    result.warnings.append(warning.toString());
  }
  if (result.summary.trimmed().isEmpty()) {
    result.error_message = QStringLiteral("Ollama proof critique must include a summary.");
    return result;
  }
  if (result.taste_confidence.trimmed().isEmpty()) {
    result.error_message = QStringLiteral("Ollama proof critique must include tasteConfidence.");
    return result;
  }
  result.ok = true;
  return result;
}

AgentLocalModelPlanResult plan_agent_recipe_patch(
    const AgentLocalModelPlanRequest& request) {
  AgentLocalModelPlanResult result;
  result.model_id = request.model_id.trimmed().isEmpty() ? default_agent_local_model_id()
                                                         : request.model_id.trimmed();
  if (result.model_id != QString::fromLatin1(kRulebookDeterministicModelId)) {
    const auto model = model_metadata_by_id(result.model_id);
    if (model.isEmpty()) {
      result.error_message = QStringLiteral("Unsupported local model: %1").arg(result.model_id);
      return result;
    }
    if (model.value(QStringLiteral("backend")).toString() == QStringLiteral("ollama")) {
      if (!model.value(QStringLiteral("available")).toBool()) {
        result.error_message =
            model.value(QStringLiteral("statusReason")).toString().trimmed().isEmpty()
                ? QStringLiteral("Ollama model is not available: %1").arg(result.model_id)
                : model.value(QStringLiteral("statusReason")).toString();
        return result;
      }
      return plan_ollama_recipe_patch(request, model);
    }
    result.error_message = QStringLiteral("Unsupported local model backend: %1")
                               .arg(model.value(QStringLiteral("backend")).toString());
    return result;
  }
  if (request.style.isEmpty()) {
    result.error_message = QStringLiteral("A valid style is required before planning an agent edit.");
    return result;
  }

  const auto style_id = request.style.value(QStringLiteral("id")).toString();
  const auto goal = request.style.value(QStringLiteral("goal")).toString();
  const auto strategy_phase = normalized_strategy_phase(request.strategy_phase);
  const auto strategy_prompt = strategy_prompt_for_phase(strategy_phase);
  const auto few_shot_examples = request.few_shot_examples.isEmpty()
                                     ? compact_few_shot_examples_from_memory(request.memory_packet)
                                     : request.few_shot_examples;
  const auto combined_intent =
      QStringLiteral("%1\n%2\n%3\n%4")
          .arg(style_id, goal, strategy_prompt, request.user_instructions);
  result.prompt_audit = QVariantMap{
      {QStringLiteral("backend"), QStringLiteral("local")},
      {QStringLiteral("model"), QString::fromLatin1(kRulebookDeterministicModelId)},
      {QStringLiteral("schemaMode"), QStringLiteral("deterministic-rulebook")},
      {QStringLiteral("strategyPhase"), strategy_phase},
      {QStringLiteral("strategyPrompt"), strategy_prompt},
      {QStringLiteral("deterministicPlanning"), true},
      {QStringLiteral("temperature"), 0.0},
      {QStringLiteral("systemPrompt"), QStringLiteral("Deterministic local planner applies the selected style rulebook and user instructions without network calls.")},
      {QStringLiteral("userPrompt"), combined_intent},
      {QStringLiteral("userPromptCharCount"), combined_intent.size()},
      {QStringLiteral("availableToolCount"), request.available_tool_schemas.size()},
      {QStringLiteral("priorToolResultCount"), request.prior_tool_results.size()},
      {QStringLiteral("toolCallPlanningAllowed"), request.allow_tool_call_plan},
      {QStringLiteral("memoryPacketPhase"), request.memory_packet.value(QStringLiteral("phase"))},
      {QStringLiteral("includedMemoryKinds"),
       request.memory_packet.value(QStringLiteral("includedMemoryKinds"))},
      {QStringLiteral("rawMemoryVaultIncluded"),
       request.memory_packet.value(QStringLiteral("rawVaultIncluded")).toBool()},
      {QStringLiteral("fewShotExampleCount"), few_shot_examples.size()},
      {QStringLiteral("imageIncluded"), false},
      {QStringLiteral("imageByteCount"), 0},
  };
  const auto max_exposure = number_constraint(
      request.style,
      QStringLiteral("max_exposure_shift"),
      1.0,
      0.05,
      2.0);
  const auto max_saturation = number_constraint(
      request.style,
      QStringLiteral("max_saturation_shift"),
      16.0,
      1.0,
      40.0);
  const auto max_temperature = number_constraint(
      request.style,
      QStringLiteral("max_temperature_shift"),
      2000.0,
      100.0,
      10000.0) /
      100.0;
  const auto temperature_bias = string_color_constraint(
      request.style,
      QStringLiteral("preferred_temperature_bias"),
      QString());
  const auto max_hsl_channel_shift = number_color_constraint(
      request.style,
      QStringLiteral("max_hsl_channel_shift"),
      0.0,
      0.0,
      60.0);
  const auto max_color_grading_saturation = number_color_constraint(
      request.style,
      QStringLiteral("max_color_grading_saturation"),
      0.0,
      0.0,
      60.0);
  const auto allow_harmony = bool_color_constraint(
      request.style,
      QStringLiteral("allow_harmony"),
      false);
  result.style_constraint_citations = cited_style_constraints(request.style);

  auto exposure = 0.35;
  auto contrast = 10.0;
  auto highlights = -18.0;
  auto shadows = 14.0;
  auto whites = -6.0;
  auto blacks = -7.0;
  auto temperature = 6.0;
  auto tint = 1.5;
  auto vibrance = 10.0;
  auto saturation = 2.0;
  auto curve_lights = 8.0;
  auto curve_highlights = -6.0;
  auto clarity = 4.0;
  auto texture = 3.0;
  auto sharpening = 18.0;

  if (text_contains(combined_intent, QStringLiteral("warm")) ||
      text_contains(combined_intent, QStringLiteral("wedding")) ||
      text_contains(combined_intent, QStringLiteral("editorial"))) {
    temperature = 12.0;
    tint = 2.5;
    vibrance = 12.0;
    saturation = 3.0;
    highlights = -24.0;
    shadows = 18.0;
    curve_highlights = -10.0;
    result.plan_summary =
        QStringLiteral("Warm editorial draft with highlight protection, open shadows, and restrained color lift.");
  } else if (
      text_contains(combined_intent, QStringLiteral("commercial")) ||
      text_contains(combined_intent, QStringLiteral("clean")) ||
      text_contains(combined_intent, QStringLiteral("neutral"))) {
    exposure = 0.2;
    contrast = 8.0;
    highlights = -16.0;
    shadows = 10.0;
    temperature = 0.0;
    tint = 0.0;
    vibrance = 5.0;
    saturation = 0.0;
    clarity = 5.0;
    texture = 4.0;
    result.plan_summary =
        QStringLiteral("Clean neutral draft with controlled contrast, accurate color, and crisp detail.");
  } else {
    result.plan_summary =
        QStringLiteral("Balanced natural draft with conservative tone, color, and detail adjustments.");
  }

  if (text_contains(combined_intent, QStringLiteral("black and white")) ||
      text_contains(combined_intent, QStringLiteral("monochrome"))) {
    saturation = -100.0;
    vibrance = 0.0;
    contrast = 16.0;
    result.plan_summary = QStringLiteral("Monochrome draft with stronger tonal separation.");
  }
  if (text_contains(combined_intent, QStringLiteral("bright"))) {
    exposure += 0.18;
    shadows += 8.0;
  }
  if (text_contains(combined_intent, QStringLiteral("moody"))) {
    exposure -= 0.18;
    blacks -= 10.0;
    contrast += 8.0;
  }
  if (text_contains(combined_intent, QStringLiteral("soft"))) {
    clarity -= 4.0;
    texture -= 2.0;
    sharpening = 12.0;
  }

  exposure = bounded(exposure, max_exposure);
  if (temperature_bias == QStringLiteral("warm")) {
    temperature = std::max(temperature, std::min(8.0, max_temperature));
  } else if (temperature_bias == QStringLiteral("cool")) {
    temperature = std::min(temperature, -std::min(8.0, max_temperature));
  } else if (temperature_bias == QStringLiteral("neutral")) {
    temperature = 0.0;
  }
  temperature = bounded(temperature, max_temperature);
  saturation = bounded(saturation, max_saturation);
  vibrance = std::clamp(vibrance, -max_saturation, max_saturation);

  QVariantMap recipe_patch{
      {QStringLiteral("globalTone"),
       QVariantMap{
           {QStringLiteral("exposure"), exposure},
           {QStringLiteral("contrast"), contrast},
           {QStringLiteral("highlights"), highlights},
           {QStringLiteral("shadows"), shadows},
           {QStringLiteral("whites"), whites},
           {QStringLiteral("blacks"), blacks},
       }},
      {QStringLiteral("color"),
       QVariantMap{
           {QStringLiteral("temperature"), temperature},
           {QStringLiteral("tint"), tint},
           {QStringLiteral("vibrance"), vibrance},
           {QStringLiteral("saturation"), saturation},
       }},
      {QStringLiteral("curves"),
       QVariantMap{
           {QStringLiteral("curveLights"), curve_lights},
           {QStringLiteral("curveHighlights"), curve_highlights},
       }},
      {QStringLiteral("detail"),
       QVariantMap{
           {QStringLiteral("texture"), texture},
           {QStringLiteral("clarity"), clarity},
           {QStringLiteral("sharpening"), sharpening},
       }},
  };
  if (style_tool_enabled(request.style, QStringLiteral("hsl")) &&
      max_hsl_channel_shift > 0.0) {
    auto channel = QStringLiteral("Reds");
    auto channel_prefix = QStringLiteral("reds");
    if (text_contains(combined_intent, QStringLiteral("sky")) ||
        text_contains(combined_intent, QStringLiteral("blue"))) {
      channel = QStringLiteral("Blues");
      channel_prefix = QStringLiteral("blues");
    } else if (
        text_contains(combined_intent, QStringLiteral("green")) ||
        text_contains(combined_intent, QStringLiteral("landscape"))) {
      channel = QStringLiteral("Greens");
      channel_prefix = QStringLiteral("greens");
    } else if (
        text_contains(combined_intent, QStringLiteral("skin")) ||
        text_contains(combined_intent, QStringLiteral("portrait")) ||
        text_contains(combined_intent, QStringLiteral("wedding"))) {
      channel = QStringLiteral("Oranges");
      channel_prefix = QStringLiteral("oranges");
    }
    QVariantMap color_mixer_patch{
        {QStringLiteral("colorMixerModel"), QStringLiteral("OKLab")},
        {QStringLiteral("hslChannel"), channel},
        {QStringLiteral("%1Saturation").arg(channel_prefix),
         std::clamp(vibrance * 0.45, -max_hsl_channel_shift, max_hsl_channel_shift)},
        {QStringLiteral("%1Luminance").arg(channel_prefix),
         std::clamp(shadows * 0.20, -max_hsl_channel_shift, max_hsl_channel_shift)},
    };
    if (protected_hues_contains(request.style, QStringLiteral("skin"))) {
      color_mixer_patch.insert(QStringLiteral("skinToneProtectionEnabled"), true);
      color_mixer_patch.insert(QStringLiteral("skinToneLineHue"), 0.08);
      color_mixer_patch.insert(QStringLiteral("skinToneProtectionRange"), 34.0);
      color_mixer_patch.insert(
          QStringLiteral("skinToneProtectionStrength"),
          std::clamp(max_hsl_channel_shift * 4.0, 20.0, 70.0));
    }
    recipe_patch.insert(QStringLiteral("colorMixer"), color_mixer_patch);
  }
  if (style_tool_enabled(request.style, QStringLiteral("color_grading")) &&
      max_color_grading_saturation > 0.0) {
    QVariantMap color_grading_patch{
        {QStringLiteral("gradingShadowsHue"), temperature >= 0.0 ? 0.58 : 0.62},
        {QStringLiteral("gradingShadowsSaturation"), std::min(6.0, max_color_grading_saturation)},
        {QStringLiteral("gradingMidtonesHue"), temperature >= 0.0 ? 0.10 : 0.56},
        {QStringLiteral("gradingMidtonesSaturation"), std::min(4.0, max_color_grading_saturation)},
        {QStringLiteral("gradingHighlightsHue"), temperature >= 0.0 ? 0.12 : 0.54},
        {QStringLiteral("gradingHighlightsSaturation"), std::min(5.0, max_color_grading_saturation)},
        {QStringLiteral("gradingAmount"), std::min(32.0, max_color_grading_saturation * 2.5)},
        {QStringLiteral("gradingBalance"), temperature >= 0.0 ? 8.0 : -8.0},
    };
    if (allow_harmony) {
      color_grading_patch.insert(QStringLiteral("colorHarmonyEnabled"), true);
      color_grading_patch.insert(
          QStringLiteral("colorHarmonyMode"),
          temperature >= 0.0 ? QStringLiteral("Analogous") : QStringLiteral("Complementary"));
      color_grading_patch.insert(QStringLiteral("colorHarmonyBaseHue"), temperature >= 0.0 ? 0.10 : 0.58);
      color_grading_patch.insert(
          QStringLiteral("colorHarmonyStrength"),
          std::min(40.0, max_color_grading_saturation * 3.0));
    }
    recipe_patch.insert(QStringLiteral("colorGrading"), color_grading_patch);
  }
  result.recipe_patch = recipe_patch;
  result.decision = QStringLiteral("recipe_patch");
  result.ok = true;
  return result;
}

}  // namespace photo_editor::desktop
