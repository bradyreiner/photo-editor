#include "agent_local_model.h"
#include "agent_runtime_paths.h"
#include "agent_model_artifact.h"
#include "agent_workmanship.h"

#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QMetaType>
#include <QRegularExpression>
#include <QSet>
#include <QSize>
#include <QTcpSocket>
#include <QThread>
#include <QUrl>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>

#include "agent_edit_recipe_patch.h"

namespace photo_editor::desktop {
namespace {

constexpr auto kRulebookDeterministicModelId = "rulebook-deterministic";
constexpr auto kFastLocalVisionPlannerId = "fast-local-vision";
constexpr auto kCloudModelOptInEnv = "PHOTO_EDITOR_ENABLE_CLOUD_MODELS";
// Generation and visual review wait until completion or explicit cancellation.
constexpr int kDefaultOllamaRequestTimeoutMs = -1;
constexpr int kMinimumOllamaRequestTimeoutMs = 30000;
constexpr int kMaximumOllamaRequestTimeoutMs = 600000;
constexpr int kDefaultOllamaContextTokens = 12288;
constexpr int kDefaultOllamaMaxImageEdge = 768;
constexpr int kDefaultOllamaPlanningImageEdge = 512;
constexpr int kDefaultOllamaPlanningMaxOutputTokens = 640;

// Readiness of an explicitly chosen existing Ollama connection. Keep this
// separate from managed artifact verification and bind it to the exact target.
struct ExistingOllamaProbes {
  QReadWriteLock lock;
  QHash<QString, QVariantMap> reports;
};

ExistingOllamaProbes& existing_ollama_probes() {
  static ExistingOllamaProbes probes;
  return probes;
}

QVariantMap existing_ollama_probe(const QVariantMap& model) {
  auto& probes = existing_ollama_probes();
  QReadLocker guard(&probes.lock);
  const auto report = probes.reports.value(model.value(QStringLiteral("id")).toString());
  if (report.value(QStringLiteral("endpoint")) != model.value(QStringLiteral("endpoint")) ||
      report.value(QStringLiteral("ollamaModel")) != model.value(QStringLiteral("model"))) {
    return {};
  }
  return report;
}

void remember_existing_ollama_probe(const QVariantMap& model, const QVariantMap& report) {
  if (!model.value(QStringLiteral("existingOllamaCandidate")).toBool()) return;
  auto& probes = existing_ollama_probes();
  QWriteLocker guard(&probes.lock);
  probes.reports.insert(model.value(QStringLiteral("id")).toString(), report);
}

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
  const auto disk_path =
      QDir(configured_app_data_root()).filePath(QStringLiteral("agent-models/models.json"));
  if (QFileInfo::exists(disk_path)) {
    return disk_path;
  }
  const auto bundled_path = QStringLiteral(":/photo-editor/agent-models/models.json");
  if (QFileInfo::exists(bundled_path)) {
    return bundled_path;
  }
  return disk_path;
}

int ollama_request_timeout_ms(const QVariantMap& model) {
  const auto configured_timeout =
      model.value(QStringLiteral("requestTimeoutMs"), kDefaultOllamaRequestTimeoutMs).toInt();
  if (configured_timeout < 0) return -1;
  return std::clamp(
      configured_timeout,
      kMinimumOllamaRequestTimeoutMs,
      kMaximumOllamaRequestTimeoutMs);
}

int ollama_context_tokens(const QVariantMap& model) {
  return std::clamp(
      model.value(QStringLiteral("contextWindow"), kDefaultOllamaContextTokens).toInt(),
      4096,
      32768);
}

int ollama_max_image_edge(const QVariantMap& model) {
  return std::clamp(
      model.value(QStringLiteral("maxImageEdge"), kDefaultOllamaMaxImageEdge).toInt(),
      512,
      1600);
}

int ollama_planning_image_edge(const QVariantMap& model) {
  const auto configured = model.value(
      QStringLiteral("planningImageEdge"),
      kDefaultOllamaPlanningImageEdge).toInt();
  return std::clamp(configured, 512, ollama_max_image_edge(model));
}

int ollama_planning_max_output_tokens(const QVariantMap& model) {
  return std::clamp(
      model.value(
               QStringLiteral("planningMaxOutputTokens"),
               kDefaultOllamaPlanningMaxOutputTokens)
          .toInt(),
      512,
      1536);
}

int ollama_effective_context_tokens(
    const QVariantMap& model,
    qsizetype prompt_char_count,
    bool image_included,
    int* estimated_context_tokens_before_cap = nullptr) {
  const auto configured = ollama_context_tokens(model);
  const auto estimated_text_tokens = static_cast<int>(prompt_char_count / 3);
  const auto visual_and_output_reserve = image_included ? 5120 : 2048;
  const auto estimated_required = estimated_text_tokens + visual_and_output_reserve;
  const auto rounded_required =
      ((std::max(estimated_required, 4096) + 4095) / 4096) * 4096;
  if (estimated_context_tokens_before_cap)
    *estimated_context_tokens_before_cap = rounded_required;
  return std::clamp(std::max(configured, rounded_required), 4096, 24576);
}

QString ollama_recognized_visual_model_family(const QString& model_name) {
  auto normalized = model_name.toLower();
  normalized.remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
  const auto matches_family = [&normalized](const QString& family) {
    if (!normalized.startsWith(family)) return false;
    return normalized.size() == family.size() || normalized.at(family.size()).isDigit();
  };
  if (matches_family(QStringLiteral("qwen25vl"))) return QStringLiteral("qwen2.5vl");
  if (matches_family(QStringLiteral("qwen3vl"))) return QStringLiteral("qwen3vl");
  if (matches_family(QStringLiteral("qwen35"))) return QStringLiteral("qwen3.5");
  if (matches_family(QStringLiteral("qwen36"))) return QStringLiteral("qwen3.6");
  return {};
}

QVariantList ollama_image_context_reservation_rows(
    const QString& model_name,
    const QList<QSize>& transmitted_sizes,
    int attached_image_count) {
  QVariantList reservations;
  const auto family = ollama_recognized_visual_model_family(model_name);
  const auto count = std::max(0, attached_image_count);
  reservations.reserve(count);
  for (int index = 0; index < count; ++index) {
    const auto dimensions_available = index < transmitted_sizes.size();
    const auto size = dimensions_available ? transmitted_sizes.at(index) : QSize{};
    const auto dimensions_valid = dimensions_available && size.width() > 0 && size.height() > 0;
    const auto small_known_family_image = !family.isEmpty() && dimensions_valid &&
        size.width() <= 768 && size.height() <= 768;
    // Qwen3.5/3.6 use patch=16 and merge=2 (32px token alignment).
    // Ceiling both edges bounds alignment; 512 overhead and a 2048 floor
    // retain a cushion. Unknown processors and edges >1600 stay at 4096.
    const auto qwen35_grid_image = dimensions_valid &&
        (family == QStringLiteral("qwen3.5") || family == QStringLiteral("qwen3.6")) &&
        size.width() <= 1600 && size.height() <= 1600;
    const auto grid_tokens = qwen35_grid_image
        ? ((size.width() + 31) / 32) * ((size.height() + 31) / 32) : 0;
    const auto reserve_tokens = qwen35_grid_image
        ? std::max(2048, ((grid_tokens + 512 + 511) / 512) * 512)
        : (small_known_family_image ? 2048 : 4096);
    reservations.append(QVariantMap{
        {QStringLiteral("imageIndex"), index},
        {QStringLiteral("modelFamily"), family.isEmpty()
             ? QStringLiteral("unrecognized") : family},
        {QStringLiteral("transmittedWidth"), dimensions_available ? size.width() : 0},
        {QStringLiteral("transmittedHeight"), dimensions_available ? size.height() : 0},
        {QStringLiteral("dimensionsValid"), dimensions_valid},
        {QStringLiteral("reserveTokens"), reserve_tokens},
        {QStringLiteral("gridAlignmentPixels"), qwen35_grid_image ? 32 : 0},
        {QStringLiteral("ceilingGridTokens"), grid_tokens},
        {QStringLiteral("reservationRule"), qwen35_grid_image
             ? QStringLiteral("qwen35_36_32px_grid_512_overhead_2048_floor")
             : small_known_family_image ? QStringLiteral("recognized_qwen_vl_at_most_768px_conservative_2048")
             : QStringLiteral("unknown_or_large_or_missing_image_conservative_4096")},
    });
  }
  return reservations;
}

int ollama_planning_context_requirement(
    const QString& system_prompt,
    const QString& user_prompt,
    const QString& model_name,
    const QList<QSize>& transmitted_image_sizes,
    int attached_image_count,
    int max_output_tokens,
    int* estimated_text_tokens = nullptr,
    qint64* ascii_utf8_bytes = nullptr,
    qint64* non_ascii_utf8_bytes = nullptr,
    qint64* non_ascii_code_point_count = nullptr,
    QVariantList* image_context_reservations = nullptr) {
  const auto system_bytes = static_cast<qint64>(system_prompt.toUtf8().size());
  const auto user_bytes = static_cast<qint64>(user_prompt.toUtf8().size());
  qint64 ascii_bytes = 0;
  qint64 non_ascii_code_points = 0;
  const auto count_text = [&ascii_bytes, &non_ascii_code_points](const QString& text) {
    for (const auto character : text.toUcs4()) {
      if (character <= 0x7f)
        ++ascii_bytes;
      else
        ++non_ascii_code_points;
    }
  };
  count_text(system_prompt);
  count_text(user_prompt);
  const auto aggregate_text_bytes = std::max<qint64>(0, system_bytes + user_bytes);
  const auto non_ascii_bytes = std::max<qint64>(0, aggregate_text_bytes - ascii_bytes);
  // English/ASCII prose is estimated at four bytes per token. Every byte in
  // non-ASCII UTF-8 text is charged as a token, which bounds byte-fallback
  // tokenizers and prevents long CJK or emoji briefs from inheriting the
  // optimistic ASCII ratio. The schema `format` grammar is separately audited.
  const auto ascii_tokens = (ascii_bytes + 3) / 4;
  const auto text_token_estimate = ascii_tokens + non_ascii_bytes;
  const auto text_tokens = static_cast<int>(std::min<qint64>(
      std::numeric_limits<int>::max(), text_token_estimate));
  if (estimated_text_tokens != nullptr) *estimated_text_tokens = text_tokens;
  if (ascii_utf8_bytes != nullptr) *ascii_utf8_bytes = ascii_bytes;
  if (non_ascii_utf8_bytes != nullptr) *non_ascii_utf8_bytes = non_ascii_bytes;
  if (non_ascii_code_point_count != nullptr) *non_ascii_code_point_count = non_ascii_code_points;
  constexpr qint64 kOtherPromptReserveTokens = 512;
  const auto reservations = ollama_image_context_reservation_rows(
      model_name, transmitted_image_sizes, attached_image_count);
  qint64 image_context_reserve = 0;
  for (const auto& reservation : reservations)
    image_context_reserve += reservation.toMap().value(QStringLiteral("reserveTokens")).toLongLong();
  if (image_context_reservations != nullptr) *image_context_reservations = reservations;
  const auto reserve = kOtherPromptReserveTokens +
      static_cast<qint64>(std::max(0, max_output_tokens)) +
      image_context_reserve;
  const auto required = static_cast<qint64>(text_tokens) + reserve;
  return static_cast<int>(std::min<qint64>(
      std::max<qint64>(required, 4096), std::numeric_limits<int>::max()));
}

QString json_object_text(const QString& candidate) {
  auto text = candidate.trimmed();
  if (text.startsWith(QStringLiteral("```"))) {
    const auto first_newline = text.indexOf(QLatin1Char('\n'));
    const auto closing_fence = text.lastIndexOf(QStringLiteral("```"));
    if (first_newline >= 0 && closing_fence > first_newline) {
      text = text.mid(first_newline + 1, closing_fence - first_newline - 1).trimmed();
    }
  }

  const auto parse_object = [](const QString& value) -> QString {
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(value.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
      return {};
    }
    return QString::fromUtf8(document.toJson(QJsonDocument::Compact));
  };

  if (const auto parsed = parse_object(text); !parsed.isEmpty()) {
    return parsed;
  }
  const auto first_brace = text.indexOf(QLatin1Char('{'));
  const auto last_brace = text.lastIndexOf(QLatin1Char('}'));
  if (first_brace >= 0 && last_brace > first_brace) {
    return parse_object(text.mid(first_brace, last_brace - first_brace + 1));
  }
  return {};
}

QString ollama_structured_response_text(
    const QJsonObject& response,
    QString* response_source = nullptr) {
  const auto message = response.value(QStringLiteral("message")).toObject();
  const QList<QPair<QString, QString>> candidates{
      {QStringLiteral("message.content"), message.value(QStringLiteral("content")).toString()},
      {QStringLiteral("response"), response.value(QStringLiteral("response")).toString()},
      // Some reasoning-capable Qwen/Ollama combinations place schema-constrained JSON
      // in `thinking` and leave `content` empty. Only accept it when it parses as one
      // complete JSON object; never expose arbitrary chain-of-thought text.
      {QStringLiteral("message.thinking"), message.value(QStringLiteral("thinking")).toString()},
  };
  for (const auto& [source, candidate] : candidates) {
    const auto structured = json_object_text(candidate);
    if (structured.isEmpty()) {
      continue;
    }
    if (response_source != nullptr) {
      *response_source = source;
    }
    return structured;
  }
  return {};
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
    const auto task_planning_protocol =
        object.value(QStringLiteral("taskPlanningProtocol")).toString().trimmed().toLower();
    if (!task_planning_protocol.isEmpty()) {
      model.insert(QStringLiteral("taskPlanningProtocol"), task_planning_protocol);
      for (const auto& key : {QStringLiteral("chatEndpoint"), QStringLiteral("chatModel"),
               QStringLiteral("chatRequestTimeoutMs"), QStringLiteral("chatContextWindow")}) {
        if (object.contains(key)) model.insert(key, object.value(key).toVariant());
      }
    }
    if (backend == QStringLiteral("ollama")) {
      model.insert(QStringLiteral("taskPlanningProtocol"),
                   task_planning_protocol.isEmpty() ? QStringLiteral("ollama-chat") : task_planning_protocol);
      model.insert(QStringLiteral("existingOllamaCandidate"),
                   object.value(QStringLiteral("existingOllamaCandidate")).toBool());
      const auto managed = object.value(QStringLiteral("runtime")).toString() == QStringLiteral("managed");
      const auto runtime = managed ? managed_agent_runtime_snapshot() : QVariantMap{};
      const auto endpoint = managed ? runtime.value(QStringLiteral("endpoint")).toString() :
          object.value(QStringLiteral("endpoint")).toString(QStringLiteral("http://127.0.0.1:11434")).trimmed();
      const auto model_name = object.value(QStringLiteral("model")).toString().trimmed();
      model.insert(QStringLiteral("runtime"), managed ? QStringLiteral("managed") : QStringLiteral("external"));
      if (managed) model.insert(QStringLiteral("runtimeState"), runtime.value(QStringLiteral("state"), QStringLiteral("stopped")));
      model.insert(QStringLiteral("endpoint"), endpoint);
      model.insert(QStringLiteral("model"), model_name);
      if (managed) {
        const auto pin = pinned_agent_model_artifact(model_name);
        model.insert(QStringLiteral("artifactLicense"), pin.license);
        model.insert(QStringLiteral("artifactBytes"), pin.downloadBytes());
        model.insert(QStringLiteral("artifactVerification"), runtime.value(QStringLiteral("modelVerification")).toMap().value(model_name));
      }
      model.insert(QStringLiteral("vision"), object.value(QStringLiteral("vision")).toBool(false));
      model.insert(
          QStringLiteral("requestTimeoutMs"),
          object.value(QStringLiteral("requestTimeoutMs")).toInt(kDefaultOllamaRequestTimeoutMs));
      model.insert(
          QStringLiteral("contextWindow"),
          object.value(QStringLiteral("contextWindow")).toInt(kDefaultOllamaContextTokens));
      model.insert(
          QStringLiteral("maxImageEdge"),
          object.value(QStringLiteral("maxImageEdge")).toInt(kDefaultOllamaMaxImageEdge));
      model.insert(
          QStringLiteral("planningImageEdge"),
          object.value(QStringLiteral("planningImageEdge"))
              .toInt(kDefaultOllamaPlanningImageEdge));
      model.insert(
          QStringLiteral("planningMaxOutputTokens"),
          object.value(QStringLiteral("planningMaxOutputTokens"))
              .toInt(kDefaultOllamaPlanningMaxOutputTokens));
      model.insert(QStringLiteral("requiresCloudOptIn"), false);
      if (model_name.isEmpty()) {
        model.insert(QStringLiteral("status"), QStringLiteral("failed"));
        model.insert(QStringLiteral("available"), false);
        model.insert(QStringLiteral("statusReason"), QStringLiteral("Ollama model manifests must include a model name."));
      } else if (managed && (!runtime.value(QStringLiteral("ready")).toBool() ||
                            !runtime.value(QStringLiteral("installedModels")).toStringList().contains(model_name))) {
        model.insert(QStringLiteral("status"), QStringLiteral("unavailable"));
        model.insert(QStringLiteral("available"), false);
        model.insert(QStringLiteral("statusReason"), runtime.value(QStringLiteral("ready")).toBool()
            ? QStringLiteral("The private runtime is ready. Download the selected vision model to continue.")
            : runtime.value(QStringLiteral("error")).toString().isEmpty()
                ? QStringLiteral("The app-owned local runtime is not ready yet.")
                : runtime.value(QStringLiteral("error")));
      } else if (managed && !runtime.value(QStringLiteral("verifiedModels")).toStringList().contains(model_name)) {
        const auto verification = runtime.value(QStringLiteral("modelVerification")).toMap().value(model_name).toMap();
        model.insert(QStringLiteral("status"), QStringLiteral("unavailable"));
        model.insert(QStringLiteral("available"), false);
        model.insert(QStringLiteral("statusReason"), verification.value(QStringLiteral("error"),
            QStringLiteral("The local model is not yet verified against an approved artifact pin.")).toString());
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
      if (model.value(QStringLiteral("existingOllamaCandidate")).toBool()) {
        const auto probe = existing_ollama_probe(model);
        model.insert(QStringLiteral("available"), probe.value(QStringLiteral("available")).toBool());
        model.insert(QStringLiteral("status"), probe.value(QStringLiteral("available")).toBool()
            ? QStringLiteral("available") : QStringLiteral("unavailable"));
        model.insert(QStringLiteral("statusReason"), probe.value(QStringLiteral("available")).toBool()
            ? QString() : QStringLiteral("Check the model already installed in Ollama on this Mac."));
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

QString first_matching_value(
    const QString& text,
    const QList<QPair<QString, QStringList>>& candidates,
    const QString& fallback) {
  for (const auto& candidate : candidates) {
    for (const auto& phrase : candidate.second) {
      if (text_contains(text, phrase)) {
        return candidate.first;
      }
    }
  }
  return fallback;
}

QVariantMap deterministic_edit_intent(
    const QString& combined_intent,
    const QVariantMap& style) {
  const auto learned_profile = style.value(QStringLiteral("learnedProfile")).toMap().isEmpty()
                                   ? style.value(QStringLiteral("learned_profile")).toMap()
                                   : style.value(QStringLiteral("learnedProfile")).toMap();
  const auto color_mode = first_matching_value(
      combined_intent,
      {
          {QStringLiteral("monochrome"),
           {QStringLiteral("black and white"), QStringLiteral("black & white"), QStringLiteral("monochrome")}},
          {QStringLiteral("color"), {QStringLiteral("in color"), QStringLiteral("colorful")}},
      },
      QStringLiteral("preserve"));
  const auto contrast = first_matching_value(
      combined_intent,
      {
          {QStringLiteral("strong"),
           {QStringLiteral("super contrast"), QStringLiteral("high contrast"),
            QStringLiteral("strong contrast"), QStringLiteral("contrasty")}},
          {QStringLiteral("soft"),
           {QStringLiteral("low contrast"), QStringLiteral("soft contrast"),
            QStringLiteral("softer contrast"), QStringLiteral("less contrast"),
            QStringLiteral("quieter"), QStringLiteral("quiet")}},
      },
      QStringLiteral("natural"));
  const auto crop = first_matching_value(
      combined_intent,
      {
          {QStringLiteral("improve"),
           {QStringLiteral("better crop"), QStringLiteral("improve the crop"),
            QStringLiteral("improve crop"), QStringLiteral("reframe")}},
          {QStringLiteral("tighter"), {QStringLiteral("tighter crop"), QStringLiteral("crop tighter")}},
      },
      QStringLiteral("preserve"));
  const auto brightness = first_matching_value(
      combined_intent,
      {
          {QStringLiteral("brighter"), {QStringLiteral("bright"), QStringLiteral("lighter")}},
          {QStringLiteral("darker"), {QStringLiteral("dark"), QStringLiteral("moody")}},
      },
      QStringLiteral("preserve"));
  const auto detail = first_matching_value(
      combined_intent,
      {
          {QStringLiteral("soft"), {QStringLiteral("soft"), QStringLiteral("dreamy")}},
          {QStringLiteral("crisp"), {QStringLiteral("crisp"), QStringLiteral("sharp"), QStringLiteral("detailed")}},
      },
      QStringLiteral("natural"));
  const auto palette = first_matching_value(
      combined_intent,
      {
          {QStringLiteral("warm"), {QStringLiteral("warm"), QStringLiteral("golden")}},
          {QStringLiteral("cool"), {QStringLiteral("cool"), QStringLiteral("blue tone")}},
          {QStringLiteral("neutral"), {QStringLiteral("neutral"), QStringLiteral("accurate color")}},
      },
      learned_profile.isEmpty() ? QStringLiteral("preserve") : QStringLiteral("reference"));
  const auto saturation = first_matching_value(
      combined_intent,
      {
          {QStringLiteral("more"),
           {QStringLiteral("more saturated"), QStringLiteral("more vibrant"),
            QStringLiteral("richer color"), QStringLiteral("richer colours"),
            QStringLiteral("boost the color"), QStringLiteral("boost color")}},
          {QStringLiteral("less"),
           {QStringLiteral("less saturated"), QStringLiteral("less vibrant"),
            QStringLiteral("muted color"), QStringLiteral("muted colours"),
            QStringLiteral("restrained color")}},
      },
      QStringLiteral("preserve"));
  const auto mood = first_matching_value(
      combined_intent,
      {
          {QStringLiteral("editorial"), {QStringLiteral("editorial"), QStringLiteral("wedding")}},
          {QStringLiteral("clean"), {QStringLiteral("commercial"), QStringLiteral("clean")}},
          {QStringLiteral("moody"), {QStringLiteral("moody"), QStringLiteral("dramatic")}},
      },
      learned_profile.isEmpty() ? QStringLiteral("natural") : QStringLiteral("reference"));

  QStringList requested_actions{QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")};
  if (crop != QStringLiteral("preserve")) {
    requested_actions.append(QStringLiteral("crop"));
  }
  QVariantList approvals;
  if (crop == QStringLiteral("tighter")) {
    approvals.append(QStringLiteral("large_crop_if_subject_geometry_requires_it"));
  }
  return QVariantMap{
      {QStringLiteral("schemaVersion"), 1},
      {QStringLiteral("goalSummary"), combined_intent.trimmed().left(360)},
      {QStringLiteral("colorMode"), color_mode},
      {QStringLiteral("contrast"), contrast},
      {QStringLiteral("brightness"), brightness},
      {QStringLiteral("palette"), palette},
      {QStringLiteral("saturation"), saturation},
      {QStringLiteral("crop"), crop},
      {QStringLiteral("detail"), detail},
      {QStringLiteral("mood"), mood},
      {QStringLiteral("referenceStrength"), learned_profile.isEmpty() ? 0.0 : 1.0},
      {QStringLiteral("requestedActions"), requested_actions},
      {QStringLiteral("requiresApproval"), approvals},
      {QStringLiteral("confidence"), 1.0},
      {QStringLiteral("globalChangePolicy"), QStringLiteral("allow")},
      {QStringLiteral("visualFindings"), QVariantList{}},
      {QStringLiteral("localizedActions"), QVariantList{}},
      {QStringLiteral("retouchActions"), QVariantList{}},
      {QStringLiteral("source"), QStringLiteral("deterministic-language-parser")},
  };
}

QString normalized_intent_enum(
    const QVariantMap& intent,
    const QString& key,
    const QStringList& allowed,
    const QString& fallback) {
  const auto value = intent.value(key).toString().trimmed().toLower();
  return allowed.contains(value) ? value : fallback;
}

QVariantMap normalized_visual_region(
    const QVariantMap& raw_region,
    bool include_feather) {
  if (raw_region.isEmpty()) {
    return {};
  }
  const auto normalized_number = [&raw_region](
                                     const QString& key,
                                     double fallback,
                                     double minimum,
                                     double maximum) {
    auto ok = false;
    const auto value = raw_region.value(key).toDouble(&ok);
    return ok && std::isfinite(value) ? std::clamp(value, minimum, maximum)
                                      : fallback;
  };
  QVariantMap region{
      {QStringLiteral("centerX"),
       normalized_number(QStringLiteral("centerX"), 0.5, 0.0, 1.0)},
      {QStringLiteral("centerY"),
       normalized_number(QStringLiteral("centerY"), 0.5, 0.0, 1.0)},
      {QStringLiteral("radiusX"),
       normalized_number(QStringLiteral("radiusX"), 0.25, 0.04, 0.5)},
      {QStringLiteral("radiusY"),
       normalized_number(QStringLiteral("radiusY"), 0.25, 0.04, 0.5)},
  };
  if (include_feather) {
    region.insert(
        QStringLiteral("feather"),
        normalized_number(QStringLiteral("feather"), 0.55, 0.05, 1.0));
  }
  return region;
}

QVariantList normalized_visual_findings(const QVariantList& raw_findings) {
  static const QStringList allowed_kinds{
      QStringLiteral("exposure"),
      QStringLiteral("highlight_clipping"),
      QStringLiteral("shadow_crush"),
      QStringLiteral("color_cast"),
      QStringLiteral("skin_tone"),
      QStringLiteral("subject_separation"),
      QStringLiteral("background_distraction"),
      QStringLiteral("composition"),
      QStringLiteral("detail"),
  };
  static const QStringList allowed_targets{
      QStringLiteral("whole_image"),
      QStringLiteral("subject"),
      QStringLiteral("face"),
      QStringLiteral("skin"),
      QStringLiteral("background"),
      QStringLiteral("sky"),
      QStringLiteral("foreground"),
      QStringLiteral("object"),
  };
  static const QStringList allowed_severities{
      QStringLiteral("low"),
      QStringLiteral("medium"),
      QStringLiteral("high"),
  };
  QVariantList findings;
  for (const auto& value : raw_findings) {
    if (findings.size() >= 4) {
      break;
    }
    const auto raw = value.toMap();
    const auto observation =
        raw.value(QStringLiteral("observation")).toString().trimmed().left(240);
    const auto region = normalized_visual_region(
        raw.value(QStringLiteral("region")).toMap(), false);
    if (observation.isEmpty() || region.isEmpty()) {
      continue;
    }
    const auto kind = normalized_intent_enum(
        raw,
        QStringLiteral("kind"),
        allowed_kinds,
        QStringLiteral("exposure"));
    const auto target = normalized_intent_enum(
        raw,
        QStringLiteral("target"),
        allowed_targets,
        QStringLiteral("whole_image"));
    const auto severity = normalized_intent_enum(
        raw,
        QStringLiteral("severity"),
        allowed_severities,
        QStringLiteral("low"));
    auto confidence = raw.value(QStringLiteral("confidence"), 0.5).toDouble();
    confidence = std::isfinite(confidence)
                     ? std::clamp(confidence, 0.0, 1.0)
                     : 0.5;
    auto finding = QVariantMap{
        {QStringLiteral("kind"), kind},
        {QStringLiteral("target"), target},
        {QStringLiteral("severity"), severity},
        {QStringLiteral("observation"), observation},
        {QStringLiteral("confidence"), confidence},
        {QStringLiteral("region"), region},
    };
    const auto target_label =
        raw.value(QStringLiteral("targetLabel")).toString().simplified().left(100);
    if (!target_label.isEmpty()) {
      finding.insert(QStringLiteral("targetLabel"), target_label);
    } else if (target == QStringLiteral("object")) {
      finding.insert(QStringLiteral("targetLabel"), QStringLiteral("Object"));
    }
    findings.append(finding);
  }
  return findings;
}

QString localized_mask_id_for_action(QVariantMap action) {
  action.remove(QStringLiteral("maskId"));
  const auto target =
      action.value(QStringLiteral("target")).toString().trimmed().toLower();
  const auto action_bytes =
      QJsonDocument::fromVariant(action).toJson(QJsonDocument::Compact);
  const auto action_hash =
      QCryptographicHash::hash(action_bytes, QCryptographicHash::Sha256)
          .toHex()
          .left(10);
  return QStringLiteral("agent-localized-%1-%2")
      .arg(target.left(24), QString::fromLatin1(action_hash));
}

QVariantList normalized_localized_actions(const QVariantList& raw_actions) {
  static const QStringList allowed_targets{
      QStringLiteral("subject"),
      QStringLiteral("face"),
      QStringLiteral("skin"),
      QStringLiteral("background"),
      QStringLiteral("sky"),
      QStringLiteral("foreground"),
      QStringLiteral("object"),
  };
  static const QStringList allowed_shapes{
      QStringLiteral("radial"),
  };
  struct AdjustmentLimit final {
    const char* key;
    double minimum;
    double maximum;
  };
  static const AdjustmentLimit adjustment_limits[] = {
      {"exposure", -1.0, 1.0},
      {"contrast", -50.0, 50.0},
      {"highlights", -50.0, 50.0},
      {"shadows", -50.0, 50.0},
      {"temperature", -30.0, 30.0},
      {"tint", -30.0, 30.0},
      {"vibrance", -30.0, 30.0},
      {"saturation", -30.0, 30.0},
      {"texture", -25.0, 25.0},
      {"clarity", -25.0, 25.0},
      {"dehaze", -25.0, 25.0},
  };
  QVariantList actions;
  for (const auto& value : raw_actions) {
    if (actions.size() >= 2) {
      break;
    }
    const auto raw = value.toMap();
    auto confidence = raw.value(QStringLiteral("confidence"), 0.0).toDouble();
    if (!std::isfinite(confidence) || confidence < 0.6) {
      continue;
    }
    confidence = std::clamp(confidence, 0.6, 1.0);
    const auto reason =
        raw.value(QStringLiteral("reason")).toString().trimmed().left(240);
    const auto region = normalized_visual_region(
        raw.value(QStringLiteral("region")).toMap(), true);
    const auto raw_adjustments =
        raw.value(QStringLiteral("adjustments")).toMap();
    QVariantMap adjustments;
    for (const auto& limit : adjustment_limits) {
      const auto key = QString::fromLatin1(limit.key);
      if (!raw_adjustments.contains(key)) {
        continue;
      }
      auto ok = false;
      const auto adjustment = raw_adjustments.value(key).toDouble(&ok);
      if (ok && std::isfinite(adjustment)) {
        adjustments.insert(
            key,
            std::clamp(adjustment, limit.minimum, limit.maximum));
      }
    }
    if (reason.isEmpty() || region.isEmpty() || adjustments.isEmpty()) {
      continue;
    }
    const auto target = normalized_intent_enum(
        raw,
        QStringLiteral("target"),
        allowed_targets,
        QStringLiteral("subject"));
    const auto target_label =
        raw.value(QStringLiteral("targetLabel")).toString().simplified().left(100);
    const auto selection_mode = normalized_intent_enum(
        raw,
        QStringLiteral("selectionMode"),
        {QStringLiteral("region"), QStringLiteral("semantic"),
         QStringLiteral("guided")},
        QStringLiteral("region"));
    QVariantMap action{
        {QStringLiteral("target"), target},
        {QStringLiteral("maskShape"),
         normalized_intent_enum(
             raw,
             QStringLiteral("maskShape"),
             allowed_shapes,
             QStringLiteral("radial"))},
        {QStringLiteral("selectionMode"), selection_mode},
        {QStringLiteral("reason"), reason},
        {QStringLiteral("confidence"), confidence},
        {QStringLiteral("region"), region},
        {QStringLiteral("adjustments"), adjustments},
    };
    if (!target_label.isEmpty()) {
      action.insert(QStringLiteral("targetLabel"), target_label);
    } else if (target == QStringLiteral("object")) {
      action.insert(QStringLiteral("targetLabel"), QStringLiteral("Object"));
    }
    if (selection_mode == QStringLiteral("guided") &&
        target == QStringLiteral("object")) {
      QVariantList candidates;
      QSet<QString> candidate_ids;
      for (const auto& candidate_value :
           raw.value(QStringLiteral("selectionCandidates")).toList()) {
        if (candidates.size() >= 3) {
          break;
        }
        const auto candidate_raw = candidate_value.toMap();
        const auto candidate_label =
            candidate_raw.value(QStringLiteral("label"))
                .toString()
                .simplified()
                .left(100);
        const auto candidate_region = normalized_visual_region(
            candidate_raw.value(QStringLiteral("region")).toMap(), true);
        auto candidate_confidence =
            candidate_raw.value(QStringLiteral("confidence"), 0.5).toDouble();
        candidate_confidence = std::isfinite(candidate_confidence)
                                   ? std::clamp(candidate_confidence, 0.0, 1.0)
                                   : 0.5;
        if (candidate_label.isEmpty() || candidate_region.isEmpty()) {
          continue;
        }
        const auto identity_bytes =
            candidate_label.toLower().toUtf8() + ':' +
            QJsonDocument::fromVariant(candidate_region)
                .toJson(QJsonDocument::Compact);
        const auto candidate_id =
            QStringLiteral("candidate-%1")
                .arg(QString::fromLatin1(
                    QCryptographicHash::hash(identity_bytes, QCryptographicHash::Sha256)
                        .toHex()
                        .left(10)));
        if (candidate_ids.contains(candidate_id)) {
          continue;
        }
        candidate_ids.insert(candidate_id);
        candidates.append(QVariantMap{
            {QStringLiteral("id"), candidate_id},
            {QStringLiteral("label"), candidate_label},
            {QStringLiteral("confidence"), candidate_confidence},
            {QStringLiteral("region"), candidate_region},
        });
      }
      if (candidates.size() >= 2) {
        action.insert(QStringLiteral("selectionCandidates"), candidates);
      }
    }
    action.insert(
        QStringLiteral("maskId"), localized_mask_id_for_action(action));
    actions.append(action);
  }
  return actions;
}

QString retouch_id_for_action(QVariantMap action) {
  action.remove(QStringLiteral("retouchId"));
  const auto action_bytes =
      QJsonDocument::fromVariant(action).toJson(QJsonDocument::Compact);
  return QStringLiteral("agent-retouch-%1")
      .arg(QString::fromLatin1(
          QCryptographicHash::hash(action_bytes, QCryptographicHash::Sha256)
              .toHex()
              .left(12)));
}

QVariantList normalized_retouch_actions(const QVariantList& raw_actions) {
  static const QStringList allowed_kinds{
      QStringLiteral("blemish"),
      QStringLiteral("distraction"),
      QStringLiteral("object"),
  };
  static const QStringList allowed_operations{
      QStringLiteral("heal"),
      QStringLiteral("remove"),
  };
  QVariantList actions;
  for (const auto& value : raw_actions) {
    if (actions.size() >= 2) {
      break;
    }
    const auto raw = value.toMap();
    const auto kind =
        raw.value(QStringLiteral("kind")).toString().trimmed().toLower();
    const auto operation =
        raw.value(QStringLiteral("operation")).toString().trimmed().toLower();
    if (!allowed_kinds.contains(kind) ||
        !allowed_operations.contains(operation) ||
        (kind == QStringLiteral("blemish") &&
         operation != QStringLiteral("heal")) ||
        (kind != QStringLiteral("blemish") &&
         operation != QStringLiteral("remove"))) {
      continue;
    }
    auto confidence = raw.value(QStringLiteral("confidence"), 0.0).toDouble();
    if (!std::isfinite(confidence) || confidence < 0.85) {
      continue;
    }
    confidence = std::clamp(confidence, 0.85, 1.0);
    const auto target_label =
        raw.value(QStringLiteral("targetLabel")).toString().simplified().left(100);
    const auto reason =
        raw.value(QStringLiteral("reason")).toString().trimmed().left(240);
    const auto raw_region = raw.value(QStringLiteral("region")).toMap();
    auto center_x_ok = false;
    auto center_y_ok = false;
    auto radius_x_ok = false;
    auto radius_y_ok = false;
    const auto center_x =
        raw_region.value(QStringLiteral("centerX")).toDouble(&center_x_ok);
    const auto center_y =
        raw_region.value(QStringLiteral("centerY")).toDouble(&center_y_ok);
    const auto radius_x =
        raw_region.value(QStringLiteral("radiusX")).toDouble(&radius_x_ok);
    const auto radius_y =
        raw_region.value(QStringLiteral("radiusY")).toDouble(&radius_y_ok);
    const auto valid_region =
        center_x_ok && center_y_ok && radius_x_ok && radius_y_ok &&
        std::isfinite(center_x) && std::isfinite(center_y) &&
        std::isfinite(radius_x) && std::isfinite(radius_y) &&
        center_x >= 0.0 && center_x <= 1.0 &&
        center_y >= 0.0 && center_y <= 1.0 &&
        radius_x >= 0.01 && radius_x <= 0.12 &&
        radius_y >= 0.01 && radius_y <= 0.12;
    if (target_label.isEmpty() || reason.isEmpty() || !valid_region) {
      continue;
    }
    QVariantMap action{
        {QStringLiteral("kind"), kind},
        {QStringLiteral("operation"), operation},
        {QStringLiteral("targetLabel"), target_label},
        {QStringLiteral("reason"), reason},
        {QStringLiteral("confidence"), confidence},
        {QStringLiteral("region"),
         QVariantMap{
             {QStringLiteral("centerX"), center_x},
             {QStringLiteral("centerY"), center_y},
             {QStringLiteral("radiusX"), radius_x},
             {QStringLiteral("radiusY"), radius_y},
         }},
    };
    action.insert(
        QStringLiteral("retouchId"), retouch_id_for_action(action));
    actions.append(action);
  }
  return actions;
}

QVariantList visual_findings_linked_to_actions(
    QVariantList findings,
    const QVariantList& actions) {
  for (auto finding_index = 0; finding_index < findings.size(); ++finding_index) {
    auto finding = findings.at(finding_index).toMap();
    const auto finding_region = finding.value(QStringLiteral("region")).toMap();
    if (finding_region.isEmpty() || actions.isEmpty()) {
      continue;
    }
    auto best_action_index = -1;
    auto best_score = std::numeric_limits<double>::max();
    for (auto action_index = 0; action_index < actions.size(); ++action_index) {
      const auto action = actions.at(action_index).toMap();
      const auto action_region = action.value(QStringLiteral("region")).toMap();
      if (action_region.isEmpty()) {
        continue;
      }
      const auto delta_x =
          finding_region.value(QStringLiteral("centerX")).toDouble() -
          action_region.value(QStringLiteral("centerX")).toDouble();
      const auto delta_y =
          finding_region.value(QStringLiteral("centerY")).toDouble() -
          action_region.value(QStringLiteral("centerY")).toDouble();
      const auto target_penalty =
          finding.value(QStringLiteral("target")).toString() ==
                  action.value(QStringLiteral("target")).toString()
              ? 0.0
              : 0.18;
      const auto finding_label =
          finding.value(QStringLiteral("targetLabel")).toString().trimmed().toLower();
      const auto action_label =
          action.value(QStringLiteral("targetLabel")).toString().trimmed().toLower();
      const auto label_penalty =
          finding_label.isEmpty() || action_label.isEmpty() || finding_label == action_label
              ? 0.0
              : 0.22;
      const auto score =
          delta_x * delta_x + delta_y * delta_y + target_penalty + label_penalty;
      if (score < best_score) {
        best_score = score;
        best_action_index = action_index;
      }
    }
    if (best_action_index < 0 ||
        (actions.size() > 1 && best_score > 0.28)) {
      continue;
    }
    const auto action = actions.at(best_action_index).toMap();
    const auto mask_id = action.value(QStringLiteral("maskId")).toString();
    if (mask_id.isEmpty()) {
      continue;
    }
    finding.insert(QStringLiteral("maskId"), mask_id);
    finding.insert(QStringLiteral("localizedActionIndex"), best_action_index);
    findings[finding_index] = finding;
  }
  return findings;
}

QVariantMap normalized_model_edit_intent(const QVariantMap& raw_intent) {
  if (raw_intent.isEmpty()) {
    return {};
  }
  const auto localized_actions = normalized_localized_actions(
      raw_intent.value(QStringLiteral("localizedActions")).toList());
  const auto retouch_actions = normalized_retouch_actions(
      raw_intent.value(QStringLiteral("retouchActions")).toList());
  const auto visual_findings = visual_findings_linked_to_actions(
      normalized_visual_findings(
          raw_intent.value(QStringLiteral("visualFindings")).toList()),
      localized_actions);
  auto intent = QVariantMap{
      {QStringLiteral("schemaVersion"), 1},
      {QStringLiteral("goalSummary"), raw_intent.value(QStringLiteral("goalSummary")).toString().trimmed().left(360)},
      {QStringLiteral("colorMode"), normalized_intent_enum(
                                            raw_intent,
                                            QStringLiteral("colorMode"),
                                            {QStringLiteral("preserve"), QStringLiteral("color"), QStringLiteral("monochrome")},
                                            QStringLiteral("preserve"))},
      {QStringLiteral("contrast"), normalized_intent_enum(
                                           raw_intent,
                                           QStringLiteral("contrast"),
                                           {QStringLiteral("preserve"), QStringLiteral("soft"), QStringLiteral("natural"), QStringLiteral("strong")},
                                           QStringLiteral("natural"))},
      {QStringLiteral("brightness"), normalized_intent_enum(
                                             raw_intent,
                                             QStringLiteral("brightness"),
                                             {QStringLiteral("preserve"), QStringLiteral("darker"), QStringLiteral("brighter")},
                                             QStringLiteral("preserve"))},
      {QStringLiteral("palette"), normalized_intent_enum(
                                          raw_intent,
                                          QStringLiteral("palette"),
                                          {QStringLiteral("preserve"), QStringLiteral("reference"), QStringLiteral("warm"), QStringLiteral("cool"), QStringLiteral("neutral")},
                                          QStringLiteral("preserve"))},
      {QStringLiteral("saturation"), normalized_intent_enum(
                                             raw_intent,
                                             QStringLiteral("saturation"),
                                             {QStringLiteral("preserve"), QStringLiteral("less"), QStringLiteral("more")},
                                             QStringLiteral("preserve"))},
      {QStringLiteral("crop"), normalized_intent_enum(
                                       raw_intent,
                                       QStringLiteral("crop"),
                                       {QStringLiteral("preserve"), QStringLiteral("improve"), QStringLiteral("tighter")},
                                       QStringLiteral("preserve"))},
      {QStringLiteral("detail"), normalized_intent_enum(
                                         raw_intent,
                                         QStringLiteral("detail"),
                                         {QStringLiteral("preserve"), QStringLiteral("soft"), QStringLiteral("natural"), QStringLiteral("crisp")},
                                         QStringLiteral("natural"))},
      {QStringLiteral("mood"), normalized_intent_enum(
                                       raw_intent,
                                       QStringLiteral("mood"),
                                       {QStringLiteral("reference"), QStringLiteral("natural"), QStringLiteral("editorial"), QStringLiteral("clean"), QStringLiteral("moody")},
                                       QStringLiteral("natural"))},
      {QStringLiteral("referenceStrength"), std::clamp(raw_intent.value(QStringLiteral("referenceStrength"), 0.0).toDouble(), 0.0, 1.0)},
      {QStringLiteral("requestedActions"), raw_intent.value(QStringLiteral("requestedActions")).toStringList()},
      {QStringLiteral("requiresApproval"), raw_intent.value(QStringLiteral("requiresApproval")).toList()},
      {QStringLiteral("confidence"), std::clamp(raw_intent.value(QStringLiteral("confidence"), 0.75).toDouble(), 0.0, 1.0)},
      {QStringLiteral("globalChangePolicy"),
       normalized_intent_enum(
           raw_intent,
           QStringLiteral("globalChangePolicy"),
           {QStringLiteral("allow"), QStringLiteral("preserve")},
           QStringLiteral("allow"))},
      {QStringLiteral("visualFindings"), visual_findings},
      {QStringLiteral("localizedActions"), localized_actions},
      {QStringLiteral("retouchActions"), retouch_actions},
      {QStringLiteral("source"), QStringLiteral("local-model")},
  };
  const auto completion_status = raw_intent.value(QStringLiteral("completionStatus")).toString();
  if (completion_status == QStringLiteral("in_progress") ||
      completion_status == QStringLiteral("complete")) {
    intent.insert(QStringLiteral("completionStatus"), completion_status);
  }
  if (intent.value(QStringLiteral("goalSummary")).toString().isEmpty()) {
    intent.insert(QStringLiteral("goalSummary"), QStringLiteral("Apply the requested photo edit."));
  }
  return intent;
}

void apply_explicit_user_intent_constraints(
    QVariantMap* intent,
    const QString& user_instructions) {
  if (intent == nullptr) {
    return;
  }
  const auto instructions = user_instructions.trimmed().toLower();
  const auto requests_monochrome =
      instructions.contains(QStringLiteral("monochrome")) ||
      instructions.contains(QStringLiteral("black and white")) ||
      instructions.contains(QStringLiteral("black & white"));
  const auto preserves_color =
      instructions.contains(QStringLiteral("realistic color")) ||
      instructions.contains(QStringLiteral("preserve color")) ||
      instructions.contains(QStringLiteral("keep the image in color")) ||
      instructions.contains(QStringLiteral("keep it in color"));
  if (requests_monochrome) {
    intent->insert(QStringLiteral("colorMode"), QStringLiteral("monochrome"));
  } else if (preserves_color) {
    intent->insert(QStringLiteral("colorMode"), QStringLiteral("color"));
  }
  if (instructions.contains(QStringLiteral("preserve the framing")) ||
      instructions.contains(QStringLiteral("preserve framing")) ||
      instructions.contains(QStringLiteral("do not crop")) ||
      instructions.contains(QStringLiteral("don't crop")) ||
      instructions.contains(QStringLiteral("no crop"))) {
    intent->insert(QStringLiteral("crop"), QStringLiteral("preserve"));
  }
  const auto localized_only =
      (instructions.contains(QStringLiteral("only")) ||
       instructions.contains(QStringLiteral("leave the rest unchanged")) ||
       instructions.contains(QStringLiteral("remain unchanged")) ||
       instructions.contains(QStringLiteral("people unchanged")) ||
       instructions.contains(QStringLiteral("person unchanged")) ||
       instructions.contains(QStringLiteral("subject unchanged")) ||
       instructions.contains(QStringLiteral("unaffected regions"))) &&
      (!intent->value(QStringLiteral("localizedActions")).toList().isEmpty() ||
       !intent->value(QStringLiteral("retouchActions")).toList().isEmpty() ||
       instructions.contains(QStringLiteral("background")) ||
       instructions.contains(QStringLiteral("subject")) ||
       instructions.contains(QStringLiteral("people")) ||
       instructions.contains(QStringLiteral("sky")) ||
       instructions.contains(QStringLiteral("mask")));
  if (localized_only) {
    intent->insert(
        QStringLiteral("globalChangePolicy"), QStringLiteral("preserve"));
    return;
  }
  const auto explicitly_requests_global_look =
      instructions.contains(QStringLiteral("overall look")) ||
      instructions.contains(QStringLiteral("whole-image look")) ||
      instructions.contains(QStringLiteral("whole image look")) ||
      instructions.contains(QStringLiteral("entire-photo look")) ||
      instructions.contains(QStringLiteral("entire photo look")) ||
      (instructions.startsWith(QStringLiteral("create a ")) &&
       instructions.contains(QStringLiteral(" look")));
  if (explicitly_requests_global_look) {
    intent->insert(
        QStringLiteral("globalChangePolicy"), QStringLiteral("allow"));
  }
}

QVariantList localized_mask_stack_from_intent(
    const QVariantMap& intent,
    const QVariantMap& style) {
  if (!style_tool_enabled(style, QStringLiteral("masks"))) {
    return {};
  }
  QVariantList masks;
  for (const auto& value :
       intent.value(QStringLiteral("localizedActions")).toList()) {
    if (masks.size() >= 2) {
      break;
    }
    const auto action = value.toMap();
    const auto region = action.value(QStringLiteral("region")).toMap();
    const auto adjustments =
        action.value(QStringLiteral("adjustments")).toMap();
    if (region.isEmpty() || adjustments.isEmpty()) {
      continue;
    }
    const auto target =
        action.value(QStringLiteral("target")).toString().trimmed().toLower();
    auto display_target =
        action.value(QStringLiteral("targetLabel")).toString().simplified();
    if (display_target.isEmpty()) {
      display_target = target;
    }
    display_target.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!display_target.isEmpty()) {
      display_target[0] = display_target.at(0).toUpper();
    }
    auto mask_id = action.value(QStringLiteral("maskId")).toString();
    if (mask_id.isEmpty()) {
      mask_id = localized_mask_id_for_action(action);
    }
    QVariantMap mask{
        {QStringLiteral("id"), mask_id},
        {QStringLiteral("name"),
         QStringLiteral("Agent · %1").arg(
             display_target.isEmpty() ? QStringLiteral("Local adjustment")
                                      : display_target)},
        {QStringLiteral("kind"),
         action.value(QStringLiteral("maskShape")).toString()},
        {QStringLiteral("operation"), QStringLiteral("add")},
        {QStringLiteral("blendMode"), QStringLiteral("normal")},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("opacity"), 1.0},
        {QStringLiteral("density"), 1.0},
        {QStringLiteral("centerX"), region.value(QStringLiteral("centerX"))},
        {QStringLiteral("centerY"), region.value(QStringLiteral("centerY"))},
        {QStringLiteral("radiusX"), region.value(QStringLiteral("radiusX"))},
        {QStringLiteral("radiusY"), region.value(QStringLiteral("radiusY"))},
        {QStringLiteral("feather"), region.value(QStringLiteral("feather"))},
    };
    const auto selection_mode =
        action.value(QStringLiteral("selectionMode")).toString();
    if (selection_mode == QStringLiteral("semantic") &&
        (target == QStringLiteral("subject") ||
         target == QStringLiteral("background") ||
         target == QStringLiteral("sky"))) {
      mask.insert(QStringLiteral("agentSelectionMode"), selection_mode);
      mask.insert(QStringLiteral("agentSemanticTarget"), target);
    } else if (selection_mode == QStringLiteral("guided")) {
      mask.insert(QStringLiteral("agentSelectionMode"), selection_mode);
      mask.insert(
          QStringLiteral("agentSelectionLabel"),
          display_target.isEmpty() ? QStringLiteral("Object") : display_target);
      const auto selection_candidates =
          action.value(QStringLiteral("selectionCandidates")).toList();
      if (selection_candidates.size() >= 2) {
        mask.insert(
            QStringLiteral("agentSelectionCandidates"), selection_candidates);
      }
    }
    for (auto iterator = adjustments.cbegin();
         iterator != adjustments.cend();
         ++iterator) {
      mask.insert(iterator.key(), iterator.value());
    }
    masks.append(mask);
  }
  return masks;
}

QVariantList retouch_stack_from_intent(
    const QVariantMap& intent,
    const QVariantMap& style) {
  if (!style_tool_enabled(style, QStringLiteral("retouch"))) {
    return {};
  }
  QVariantList retouch_stack;
  for (const auto& value :
       intent.value(QStringLiteral("retouchActions")).toList()) {
    const auto action = value.toMap();
    const auto region = action.value(QStringLiteral("region")).toMap();
    if (region.isEmpty()) {
      continue;
    }
    const auto brush_size = std::clamp(
        2.0 * std::max(
                  region.value(QStringLiteral("radiusX")).toDouble(),
                  region.value(QStringLiteral("radiusY")).toDouble()),
        0.02,
        0.25);
    const auto operation = action.value(QStringLiteral("operation")).toString();
    const auto target_label =
        action.value(QStringLiteral("targetLabel")).toString().simplified();
    retouch_stack.append(QVariantMap{
        {QStringLiteral("id"), action.value(QStringLiteral("retouchId"))},
        {QStringLiteral("kind"), QStringLiteral("brush")},
        {QStringLiteral("name"),
         QStringLiteral("Agent · %1").arg(target_label).left(120)},
        {QStringLiteral("retouchMode"), operation},
        {QStringLiteral("removeStrength"),
         operation == QStringLiteral("heal") ? 82.0 : 100.0},
        {QStringLiteral("healTextureBlend"), 0.38},
        {QStringLiteral("healColorBlend"), 0.72},
        {QStringLiteral("agentInstruction"),
         intent.value(QStringLiteral("goalSummary")).toString().simplified().left(240)},
        {QStringLiteral("brushSize"), brush_size},
        {QStringLiteral("brushFlow"), 1.0},
        {QStringLiteral("agentRegion"), region},
        {QStringLiteral("committed"), false},
        {QStringLiteral("brushPoints"),
         QVariantList{
             QVariantMap{
                 {QStringLiteral("x"),
                  region.value(QStringLiteral("centerX"))},
                 {QStringLiteral("y"),
                  region.value(QStringLiteral("centerY"))},
                 {QStringLiteral("size"), brush_size},
                 {QStringLiteral("flow"), 1.0},
             },
         }},
    });
  }
  return retouch_stack;
}

QString canonical_instructions_for_edit_intent(const QVariantMap& intent) {
  QStringList instructions;
  const auto color_mode = intent.value(QStringLiteral("colorMode")).toString();
  if (color_mode == QStringLiteral("monochrome")) {
    instructions.append(QStringLiteral("black and white monochrome"));
  } else if (color_mode == QStringLiteral("color")) {
    instructions.append(QStringLiteral("in color"));
  }
  const auto contrast = intent.value(QStringLiteral("contrast")).toString();
  if (contrast == QStringLiteral("strong")) {
    instructions.append(QStringLiteral("strong high contrast"));
  } else if (contrast == QStringLiteral("soft")) {
    instructions.append(QStringLiteral("soft contrast"));
  }
  const auto brightness = intent.value(QStringLiteral("brightness")).toString();
  if (brightness == QStringLiteral("brighter")) {
    instructions.append(QStringLiteral("bright"));
  } else if (brightness == QStringLiteral("darker")) {
    instructions.append(QStringLiteral("dark moody"));
  }
  const auto palette = intent.value(QStringLiteral("palette")).toString();
  if (palette == QStringLiteral("warm") || palette == QStringLiteral("cool") ||
      palette == QStringLiteral("neutral")) {
    instructions.append(palette);
  } else if (palette == QStringLiteral("reference")) {
    instructions.append(QStringLiteral("follow the selected inspiration reference"));
  }
  const auto saturation = intent.value(QStringLiteral("saturation")).toString();
  if (saturation == QStringLiteral("more")) {
    instructions.append(QStringLiteral("more vibrant richer color"));
  } else if (saturation == QStringLiteral("less")) {
    instructions.append(QStringLiteral("less saturated muted color"));
  }
  const auto crop = intent.value(QStringLiteral("crop")).toString();
  if (crop == QStringLiteral("tighter")) {
    instructions.append(QStringLiteral("tighter crop"));
  } else if (crop == QStringLiteral("improve")) {
    instructions.append(QStringLiteral("better crop"));
  }
  const auto detail = intent.value(QStringLiteral("detail")).toString();
  if (detail == QStringLiteral("soft")) {
    instructions.append(QStringLiteral("soft"));
  } else if (detail == QStringLiteral("crisp")) {
    instructions.append(QStringLiteral("crisp detailed"));
  }
  const auto mood = intent.value(QStringLiteral("mood")).toString();
  if (mood != QStringLiteral("natural") && mood != QStringLiteral("reference")) {
    instructions.append(mood);
  }
  if (instructions.isEmpty()) {
    instructions.append(QStringLiteral("balanced natural edit"));
  }
  return instructions.join(QStringLiteral(", "));
}

QByteArray decode_chunked_http_body(
    const QByteArray& encoded,
    bool* complete,
    QString* decode_error) {
  if (complete != nullptr) {
    *complete = false;
  }
  QByteArray decoded;
  qsizetype position = 0;
  while (position < encoded.size()) {
    const auto line_end = encoded.indexOf("\r\n", position);
    if (line_end < 0) {
      return decoded;
    }
    auto size_text = encoded.mid(position, line_end - position).trimmed();
    const auto extension = size_text.indexOf(';');
    if (extension >= 0) {
      size_text = size_text.left(extension);
    }
    bool size_ok = false;
    const auto chunk_size = size_text.toLongLong(&size_ok, 16);
    if (!size_ok || chunk_size < 0) {
      if (decode_error != nullptr) {
        *decode_error = QStringLiteral("Ollama returned an invalid chunked HTTP response.");
      }
      return {};
    }
    position = line_end + 2;
    if (chunk_size == 0) {
      if (complete != nullptr) {
        *complete = true;
      }
      return decoded;
    }
    if (chunk_size > encoded.size() - position - 2) {
      return decoded;
    }
    decoded.append(encoded.mid(position, chunk_size));
    position += chunk_size;
    if (encoded.mid(position, 2) != QByteArrayLiteral("\r\n")) {
      if (decode_error != nullptr) {
        *decode_error = QStringLiteral("Ollama returned a malformed chunked HTTP response.");
      }
      return {};
    }
    position += 2;
  }
  return decoded;
}

QJsonObject http_post_json(
    const QString& endpoint,
    const QString& path,
    const QJsonObject& payload,
    int timeout_ms,
    QString* error_message,
    QVariantMap* transport_report = nullptr,
    const std::function<bool()>& cancellation_requested = {},
    const std::function<void(const QString&)>& stream_activity_received = {}) {
  QElapsedTimer request_timer;
  request_timer.start();
  const auto finish_transport = [&](bool ok,
                                    const QString& error_code,
                                    const QString& message,
                                    int status_code,
                                    qsizetype response_bytes,
                                    bool retryable) {
    if (transport_report == nullptr) {
      return;
    }
    transport_report->insert(QStringLiteral("ok"), ok);
    transport_report->insert(QStringLiteral("endpoint"), endpoint);
    transport_report->insert(QStringLiteral("path"), path);
    transport_report->insert(QStringLiteral("timeoutMs"), timeout_ms);
    transport_report->insert(QStringLiteral("durationMs"), request_timer.elapsed());
    transport_report->insert(QStringLiteral("statusCode"), status_code);
    transport_report->insert(QStringLiteral("responseBytes"), response_bytes);
    transport_report->insert(QStringLiteral("retryable"), retryable);
    if (!error_code.isEmpty()) {
      transport_report->insert(QStringLiteral("errorCode"), error_code);
    }
    if (!message.isEmpty()) {
      transport_report->insert(QStringLiteral("message"), message);
    }
  };
  const auto fail = [&](const QString& code,
                        const QString& message,
                        int status_code = 0,
                        qsizetype response_bytes = 0,
                        bool retryable = false) -> QJsonObject {
    if (error_message != nullptr) {
      *error_message = message;
    }
    finish_transport(false, code, message, status_code, response_bytes, retryable);
    return {};
  };
  QUrl url(endpoint.trimmed());
  if (!local_http_endpoint_valid(endpoint)) {
    return fail(
        QStringLiteral("invalid_endpoint"),
        QStringLiteral("Ollama endpoint must be a localhost http URL."));
  }
  const auto fake_transport_delay_ms = std::clamp(
      qEnvironmentVariableIntValue(
          "PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS"),
      0,
      5000);
  if (fake_transport_delay_ms > 0 &&
      (!qEnvironmentVariable("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON")
            .trimmed()
            .isEmpty() ||
       !qEnvironmentVariable("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON")
            .trimmed()
            .isEmpty())) {
    QThread::msleep(static_cast<unsigned long>(fake_transport_delay_ms));
  }
  const auto fake_response_sequence = qEnvironmentVariable("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON");
  static std::mutex fake_sequence_mutex;
  static QString previous_fake_sequence;
  static qsizetype fake_sequence_index = 0;
  std::unique_lock fake_sequence_lock(fake_sequence_mutex);
  if (previous_fake_sequence != fake_response_sequence) {
    previous_fake_sequence = fake_response_sequence;
    fake_sequence_index = 0;
  }
  if (!fake_response_sequence.trimmed().isEmpty()) {
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
      return fail(
          QStringLiteral("invalid_fake_response"),
          QStringLiteral("Fake Ollama response sequence JSON is invalid."));
    }
    const auto sequence = document.array();
    if (sequence.isEmpty()) {
      return fail(
          QStringLiteral("invalid_fake_response"),
          QStringLiteral("Fake Ollama response sequence is empty."));
    }
    const auto index = std::min(fake_sequence_index, sequence.size() - 1);
    if (fake_sequence_index < sequence.size() - 1) ++fake_sequence_index;
    const auto response = sequence.at(index).toObject();
    if (response.isEmpty()) {
      return fail(
          QStringLiteral("invalid_fake_response"),
          QStringLiteral("Fake Ollama sequence response is not an object."));
    }
    finish_transport(true, {}, {}, 200, 0, false);
    return response;
  }
  fake_sequence_lock.unlock();
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
      return fail(
          QStringLiteral("invalid_fake_response"),
          QStringLiteral("Fake Ollama response JSON is invalid."));
    }
    finish_transport(true, {}, {}, 200, 0, false);
    return document.object();
  }
  auto request_path = path;
  if (!request_path.startsWith(QLatin1Char('/'))) {
    request_path.prepend(QLatin1Char('/'));
  }

  QTcpSocket socket;
  socket.connectToHost(url.host(), static_cast<quint16>(url.port()));
  // A missing server must not consume the image-generation deadline.
  if (!socket.waitForConnected(timeout_ms < 0 ? 5000 : std::min(timeout_ms, 5000))) {
    return fail(
        QStringLiteral("connection_failed"),
        QStringLiteral("Could not reach Ollama at %1. Start Ollama and try again. (%2)")
            .arg(endpoint, socket.errorString()),
        0,
        0,
        true);
  }

  const auto body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
  QByteArray request;
  request.append("POST ");
  request.append(request_path.toUtf8());
  request.append(" HTTP/1.1\r\nHost: ");
  request.append(url.host().toUtf8());
  request.append(':');
  request.append(QByteArray::number(url.port()));
  request.append("\r\nOrigin: http://127.0.0.1\r\nAccept: application/json\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: ");
  request.append(QByteArray::number(body.size()));
  request.append("\r\n\r\n");
  request.append(body);
  const auto queued_bytes = socket.write(request);
  if (queued_bytes != request.size()) {
    return fail(
        QStringLiteral("write_failed"),
        QStringLiteral("Unable to queue the full Ollama request for writing."),
        0,
        0,
        true);
  }
  auto flushed = socket.flush();
  if (QCoreApplication::instance() != nullptr) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  QElapsedTimer write_timer;
  write_timer.start();
  while (socket.bytesToWrite() > 0 && (timeout_ms < 0 || write_timer.elapsed() < timeout_ms)) {
    if (cancellation_requested && cancellation_requested()) {
      socket.abort();
      return fail(
          QStringLiteral("cancelled"),
          QStringLiteral("Local model request canceled."),
          0,
          0,
          true);
    }
    if (socket.state() == QAbstractSocket::UnconnectedState) {
      return fail(QStringLiteral("disconnected"),
                  QStringLiteral("Ollama disconnected before the request was written."),
                  0, 0, true);
    }
    flushed = socket.flush() || flushed;
    if (QCoreApplication::instance() != nullptr) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    if (socket.bytesToWrite() > 0) {
      socket.waitForBytesWritten(10);
    }
  }
  if (socket.bytesToWrite() > 0) {
    return fail(
        QStringLiteral("write_timeout"),
        QStringLiteral("Timed out writing the local-model request: %1 bytes remain queued.")
            .arg(socket.bytesToWrite()),
        0,
        0,
        true);
  }
  if (!flushed && socket.state() != QAbstractSocket::ConnectedState) {
    return fail(
        QStringLiteral("disconnected"),
        QStringLiteral("Ollama disconnected before the request was written."),
        0,
        0,
        true);
  }

  QByteArray response;
  const auto stream_requested = payload.value(QStringLiteral("stream")).toBool() &&
                                static_cast<bool>(stream_activity_received);
  qsizetype streamed_body_bytes = 0;
  QByteArray streamed_line_buffer;
  QByteArray streamed_content;
  auto stream_activity_sent = false;
  const auto process_stream_line = [&](const QByteArray& line) {
    QJsonParseError stream_error;
    const auto stream_document = QJsonDocument::fromJson(line, &stream_error);
    if (stream_error.error != QJsonParseError::NoError || !stream_document.isObject()) return;
    const auto stream_object = stream_document.object();
    streamed_content.append(
        stream_object.value(QStringLiteral("message")).toObject()
            .value(QStringLiteral("content")).toString().toUtf8());
    if (stream_activity_sent) return;
    const auto activity_key = streamed_content.indexOf(QByteArrayLiteral("\"activity\""));
    if (activity_key < 0) return;
    const auto colon = streamed_content.indexOf(':', activity_key + 10);
    if (colon < 0) return;
    auto quote = colon + 1;
    while (quote < streamed_content.size() &&
           (streamed_content.at(quote) == ' ' || streamed_content.at(quote) == '\t' ||
            streamed_content.at(quote) == '\r' || streamed_content.at(quote) == '\n')) {
      ++quote;
    }
    if (quote >= streamed_content.size() || streamed_content.at(quote) != '"') return;
    auto end_quote = quote + 1;
    auto escaped = false;
    for (; end_quote < streamed_content.size(); ++end_quote) {
      const auto character = streamed_content.at(end_quote);
      if (!escaped && character == '"') break;
      if (!escaped && character == '\\') escaped = true;
      else escaped = false;
    }
    if (end_quote >= streamed_content.size()) return;
    const auto encoded_string = QByteArrayLiteral("[") +
                                streamed_content.mid(quote, end_quote - quote + 1) +
                                QByteArrayLiteral("]");
    QJsonParseError activity_error;
    const auto activity_document = QJsonDocument::fromJson(encoded_string, &activity_error);
    if (activity_error.error != QJsonParseError::NoError || !activity_document.isArray() ||
        activity_document.array().isEmpty()) return;
    auto activity = activity_document.array().first().toString().simplified();
    static const QStringList allowed_activity{
        QStringLiteral("Reviewing the request"),
        QStringLiteral("Checking the selected targets"),
        QStringLiteral("Identifying essential missing details"),
        QStringLiteral("Preparing the requested outcome"),
    };
    if (!allowed_activity.contains(activity)) return;
    stream_activity_sent = true;
    stream_activity_received(activity);
  };
  const auto process_stream_bytes = [&](const QByteArray& bytes) {
    if (!stream_requested || bytes.isEmpty()) return;
    streamed_line_buffer.append(bytes);
    while (true) {
      const auto line_end = streamed_line_buffer.indexOf('\n');
      if (line_end < 0) break;
      const auto line = streamed_line_buffer.left(line_end).trimmed();
      streamed_line_buffer.remove(0, line_end + 1);
      if (!line.isEmpty()) process_stream_line(line);
    }
  };
  const auto process_stream_response = [&]() {
    if (!stream_requested) return;
    const auto stream_header_end = response.indexOf("\r\n\r\n");
    if (stream_header_end < 0) return;
    const auto status_line = QString::fromLatin1(response.left(response.indexOf('\n'))).trimmed();
    const auto status_parts = status_line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const auto stream_status = status_parts.size() >= 2 ? status_parts.at(1).toInt() : 0;
    if (stream_status < 200 || stream_status >= 300) return;
    const auto stream_header = response.left(stream_header_end);
    auto stream_chunked = false;
    auto stream_content_length = -1;
    for (const auto& line_bytes : stream_header.split('\n')) {
      const auto line = QString::fromLatin1(line_bytes).trimmed();
      if (line.toLower().startsWith(QStringLiteral("content-length:"))) {
        auto ok = false;
        stream_content_length = line.mid(QStringLiteral("content-length:").size()).trimmed().toInt(&ok);
        if (!ok) stream_content_length = -1;
      } else if (line.toLower().startsWith(QStringLiteral("transfer-encoding:")) &&
                 line.contains(QStringLiteral("chunked"), Qt::CaseInsensitive)) {
        stream_chunked = true;
      }
    }
    auto body = response.mid(stream_header_end + 4);
    if (stream_chunked) {
      auto complete = false;
      QString decode_error;
      body = decode_chunked_http_body(body, &complete, &decode_error);
      if (!decode_error.isEmpty()) return;
    } else if (stream_content_length >= 0) {
      body = body.left(stream_content_length);
    }
    if (body.size() <= streamed_body_bytes) return;
    const auto delta = body.mid(streamed_body_bytes);
    streamed_body_bytes = body.size();
    process_stream_bytes(delta);
  };
  QElapsedTimer timer;
  timer.start();
  auto response_complete = false;
  while (timeout_ms < 0 || timer.elapsed() < timeout_ms) {
    if (cancellation_requested && cancellation_requested()) {
      socket.abort();
      return fail(
          QStringLiteral("cancelled"),
          QStringLiteral("Local model request canceled."),
          0,
          response.size(),
          true);
    }
    if (QCoreApplication::instance() != nullptr) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    if (socket.waitForReadyRead(25)) {
      response.append(socket.readAll());
      process_stream_response();
      const auto header_end = response.indexOf("\r\n\r\n");
      if (header_end < 0) {
        continue;
      }
      const auto header = response.left(header_end);
      auto content_length = -1;
      auto chunked = false;
      for (const auto& line_bytes : header.split('\n')) {
        const auto line = QString::fromLatin1(line_bytes).trimmed();
        if (line.toLower().startsWith(QStringLiteral("content-length:"))) {
          auto ok = false;
          content_length = line.mid(QStringLiteral("content-length:").size()).trimmed().toInt(&ok);
          if (!ok) {
            content_length = -1;
          }
        } else if (line.toLower().startsWith(QStringLiteral("transfer-encoding:")) &&
                   line.contains(QStringLiteral("chunked"), Qt::CaseInsensitive)) {
          chunked = true;
        }
      }
      if (content_length >= 0 && response.size() >= header_end + 4 + content_length) {
        response_complete = true;
        break;
      }
      if (chunked) {
        auto chunked_complete = false;
        QString chunk_error;
        decode_chunked_http_body(
            response.mid(header_end + 4),
            &chunked_complete,
            &chunk_error);
        if (!chunk_error.isEmpty()) {
          return fail(
              QStringLiteral("invalid_chunked_response"),
              chunk_error,
              0,
              response.size(),
              true);
        }
        if (chunked_complete) {
          response_complete = true;
          break;
        }
      }
    }
    if (socket.state() == QAbstractSocket::UnconnectedState) {
      response.append(socket.readAll());
      response_complete = !response.isEmpty();
      break;
    }
  }

  const auto header_end = response.indexOf("\r\n\r\n");
  if (header_end < 0) {
    if (socket.state() == QAbstractSocket::UnconnectedState) {
      return fail(QStringLiteral("disconnected"),
                  QStringLiteral("Ollama disconnected before returning a response."),
                  0, response.size(), true);
    }
    return fail(
        QStringLiteral("response_timeout"),
        QStringLiteral("The local model needed longer than %1 seconds to respond. No edits were applied; retry or use the fast fallback.")
            .arg(timeout_ms / 1000),
        0,
        response.size(),
        true);
  }
  const auto status_line = QString::fromLatin1(response.left(response.indexOf('\n'))).trimmed();
  const auto status_parts = status_line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  const auto status_code = status_parts.size() >= 2 ? status_parts.at(1).toInt() : 0;
  const auto header = response.left(header_end);
  auto content_length = -1;
  auto chunked = false;
  for (const auto& line_bytes : header.split('\n')) {
    const auto line = QString::fromLatin1(line_bytes).trimmed();
    if (line.toLower().startsWith(QStringLiteral("content-length:"))) {
      bool ok = false;
      content_length = line.mid(QStringLiteral("content-length:").size()).trimmed().toInt(&ok);
      if (!ok) {
        content_length = -1;
      }
    } else if (line.toLower().startsWith(QStringLiteral("transfer-encoding:")) &&
               line.contains(QStringLiteral("chunked"), Qt::CaseInsensitive)) {
      chunked = true;
    }
  }
  auto response_body = response.mid(header_end + 4);
  if (chunked) {
    auto chunked_complete = false;
    QString chunk_error;
    response_body = decode_chunked_http_body(response_body, &chunked_complete, &chunk_error);
    if (!chunk_error.isEmpty() || !chunked_complete) {
      return fail(
          QStringLiteral("incomplete_response"),
          chunk_error.isEmpty()
              ? QStringLiteral("Ollama closed an incomplete chunked response. Please retry.")
              : chunk_error,
          status_code,
          response.size(),
          true);
    }
  } else if (content_length >= 0) {
    if (response_body.size() < content_length) {
      return fail(
          QStringLiteral("incomplete_response"),
          QStringLiteral("Ollama closed the connection before its response was complete. Please retry."),
          status_code,
          response.size(),
          true);
    }
    response_body = response_body.left(content_length);
  } else if (!response_complete && socket.state() != QAbstractSocket::UnconnectedState) {
    return fail(
        QStringLiteral("response_timeout"),
        QStringLiteral("The local model response did not finish within %1 seconds. No edits were applied.")
            .arg(timeout_ms / 1000),
        status_code,
        response.size(),
        true);
  }
  if (stream_requested) {
    process_stream_bytes(response_body.mid(streamed_body_bytes));
    if (!streamed_line_buffer.trimmed().isEmpty()) {
      process_stream_line(streamed_line_buffer.trimmed());
      streamed_line_buffer.clear();
    }
  }
  if (status_code < 200 || status_code >= 300) {
    QString server_detail;
    const auto error_document = QJsonDocument::fromJson(response_body);
    if (error_document.isObject()) {
      server_detail = error_document.object().value(QStringLiteral("error")).toString().trimmed();
    }
    return fail(
        QStringLiteral("http_status"),
        server_detail.isEmpty()
            ? QStringLiteral("Ollama returned HTTP %1. Please retry.").arg(status_code)
            : QStringLiteral("Ollama returned HTTP %1: %2").arg(status_code).arg(server_detail),
        status_code,
        response.size(),
        status_code == 408 || status_code == 429 || status_code >= 500);
  }
  QJsonParseError parse_error;
  if (stream_requested) {
    QByteArray complete_content;
    QJsonObject final_stream_event;
    for (const auto& raw_line : response_body.split('\n')) {
      const auto line = raw_line.trimmed();
      if (line.isEmpty()) continue;
      const auto event_document = QJsonDocument::fromJson(line);
      if (!event_document.isObject()) continue;
      const auto event = event_document.object();
      final_stream_event = event;
      if (event.contains(QStringLiteral("message"))) {
        complete_content.append(event.value(QStringLiteral("message")).toObject()
                                    .value(QStringLiteral("content")).toString().toUtf8());
      }
    }
    final_stream_event.insert(
        QStringLiteral("message"),
        QJsonObject{{QStringLiteral("content"), QString::fromUtf8(complete_content)}});
    final_stream_event.insert(QStringLiteral("streamed"), true);
    finish_transport(true, {}, {}, status_code, response.size(), false);
    return final_stream_event;
  }
  const auto document = QJsonDocument::fromJson(response_body, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    return fail(
        QStringLiteral("invalid_json"),
        QStringLiteral("Ollama returned invalid JSON: %1").arg(parse_error.errorString()),
        status_code,
        response.size(),
        true);
  }
  finish_transport(true, {}, {}, status_code, response.size(), false);
  return document.object();
}

QJsonObject ollama_frozen_generation_with_output_recovery(
    const QString& endpoint, const QJsonObject& payload, int timeout_ms,
    int estimated_context_tokens, int context_token_limit,
    const std::function<bool()>& cancellation_requested,
    QString* error_message, QVariantMap* transport_report, QVariantMap* prompt_audit) {
  const auto initial_output_tokens = payload.value(QStringLiteral("options")).toObject()
      .value(QStringLiteral("num_predict")).toInt();
  QJsonObject response;
  QString response_source;
  QString content;
  QVariantList response_attempts;
  auto attempt_payload = payload;
  auto attempt_output_tokens = initial_output_tokens;
  auto attempt_context_tokens = estimated_context_tokens;
  // One read-only recovery for a proven output-limit truncation. The frozen
  // prompt, images and grammar remain identical; no tools run before admission.
  for (int attempt = 0; attempt < 2; ++attempt) {
    response = http_post_json(
        endpoint, QStringLiteral("/api/chat"), attempt_payload,
        timeout_ms, error_message, transport_report,
        cancellation_requested);
    prompt_audit->insert(QStringLiteral("transport"), *transport_report);
    response_source.clear();
    content = ollama_structured_response_text(response, &response_source);
    const auto message = response.value(QStringLiteral("message")).toObject();
    const auto content_bytes = message.value(QStringLiteral("content")).toString().toUtf8();
    const auto thinking_bytes = message.value(QStringLiteral("thinking")).toString().toUtf8();
    response_attempts.append(QVariantMap{
        {QStringLiteral("attempt"), attempt + 1},
        {QStringLiteral("maxOutputTokens"), attempt_output_tokens},
        {QStringLiteral("estimatedContextTokens"), attempt_context_tokens},
        {QStringLiteral("transport"), *transport_report},
        {QStringLiteral("done"), response.value(QStringLiteral("done")).toVariant()},
        {QStringLiteral("doneReason"), response.value(QStringLiteral("done_reason")).toVariant()},
        {QStringLiteral("evalCount"), response.value(QStringLiteral("eval_count")).toInt()},
        {QStringLiteral("promptEvalCount"), response.value(QStringLiteral("prompt_eval_count")).toInt()},
        {QStringLiteral("structuredObjectFound"), !content.isEmpty()},
        {QStringLiteral("messageContentUtf8Bytes"), content_bytes.size()},
        {QStringLiteral("messageContentSha256"), QString::fromLatin1(
             QCryptographicHash::hash(content_bytes, QCryptographicHash::Sha256).toHex())},
        {QStringLiteral("messageThinkingUtf8Bytes"), thinking_bytes.size()},
        {QStringLiteral("messageThinkingSha256"), QString::fromLatin1(
             QCryptographicHash::hash(thinking_bytes, QCryptographicHash::Sha256).toHex())},
    });
    prompt_audit->insert(QStringLiteral("responseAttempts"), response_attempts);
    prompt_audit->insert(QStringLiteral("responseAttemptCount"), response_attempts.size());
    prompt_audit->insert(QStringLiteral("responseDone"), response.value(QStringLiteral("done")).toVariant());
    prompt_audit->insert(QStringLiteral("responseDoneReason"), response.value(QStringLiteral("done_reason")).toVariant());
    if (response.isEmpty()) {
      *error_message = error_message->isEmpty()
          ? QStringLiteral("Ollama did not return a response.") : *error_message;
      return {};
    }
    const auto exhausted = response.value(QStringLiteral("done")).toBool() &&
        response.value(QStringLiteral("done_reason")).toString() == QStringLiteral("length") &&
        response.value(QStringLiteral("eval_count")).toInt() >= attempt_output_tokens;
    if (attempt != 0 || !exhausted) break;
    if (cancellation_requested && cancellation_requested()) {
      *error_message = QStringLiteral("Ollama generation was cancelled before output recovery.");
      prompt_audit->insert(QStringLiteral("errorCode"), QStringLiteral("planning_cancelled"));
      return {};
    }
    const auto input_reserve = estimated_context_tokens - initial_output_tokens;
    const auto retry_output_tokens = std::min({3072, initial_output_tokens * 2,
                                             context_token_limit - input_reserve});
    if (retry_output_tokens <= initial_output_tokens) {
      prompt_audit->insert(QStringLiteral("outputRecoveryBlockedReason"),
                                QStringLiteral("insufficient_context_headroom"));
      break;
    }
    attempt_output_tokens = retry_output_tokens;
    attempt_context_tokens = input_reserve + retry_output_tokens;
    auto options = attempt_payload.value(QStringLiteral("options")).toObject();
    options.insert(QStringLiteral("num_predict"), retry_output_tokens);
    attempt_payload.insert(QStringLiteral("options"), options);
    prompt_audit->insert(QStringLiteral("outputRecoveryReason"),
                              QStringLiteral("output_limit_termination"));
    prompt_audit->insert(QStringLiteral("outputRecoveryMaxOutputTokens"), retry_output_tokens);
    prompt_audit->insert(QStringLiteral("outputRecoveryEstimatedContextTokens"), attempt_context_tokens);
    prompt_audit->insert(QStringLiteral("outputRecoveryFrozenPrompt"), true);
  }
  return response;
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
  qsizetype original_bytes = 0;
  qsizetype transmitted_bytes = 0;
  QSize pixel_size;
};

ModelImagePayload image_payload_for_asset(const QVariantMap& asset, int max_edge) {
  const auto required_proof = asset.value(QStringLiteral("planningProofRequired")).toBool();
  const QList<QPair<QString, QString>> candidate_keys{
      {QStringLiteral("proofImagePath"), QStringLiteral("proof")},
      {QStringLiteral("currentProofPath"), QStringLiteral("current-proof")},
      {QStringLiteral("afterProofPath"), QStringLiteral("after-proof")},
      {QStringLiteral("beforeProofPath"), QStringLiteral("before-proof")},
      {QStringLiteral("filePath"), QStringLiteral("source-file")},
  };
  for (const auto& candidate : candidate_keys) {
    if (required_proof && candidate.first != QStringLiteral("proofImagePath")) continue;
    const auto file_path = asset.value(candidate.first).toString();
    if (file_path.trimmed().isEmpty()) {
      continue;
    }
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly)) {
      continue;
    }
    const auto original_bytes = file.readAll();
    if (required_proof && (asset.value(QStringLiteral("planningProofSha256")).toString().isEmpty() ||
        QString::fromLatin1(QCryptographicHash::hash(original_bytes, QCryptographicHash::Sha256).toHex()) !=
            asset.value(QStringLiteral("planningProofSha256")).toString())) return {};
    QImage image;
    image.loadFromData(original_bytes);
    if (required_proof && image.isNull()) return {};
    if (!image.isNull()) {
      const auto bounded_edge = std::max(1, max_edge);
      if (image.width() > bounded_edge || image.height() > bounded_edge) {
        image = image.scaled(
            bounded_edge,
            bounded_edge,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
      }
      QByteArray encoded;
      QBuffer buffer(&encoded);
      if (buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "JPEG", 84)) {
        return ModelImagePayload{
            QString::fromLatin1(encoded.toBase64()),
            file_path,
            candidate.second,
            original_bytes.size(),
            encoded.size(),
            image.size(),
        };
      }
    }
    return ModelImagePayload{
        QString::fromLatin1(original_bytes.toBase64()),
        file_path,
        candidate.second,
        original_bytes.size(),
        original_bytes.size(),
        {},
    };
  }
  return {};
}

struct AdditionalProofImageSet final {
  bool valid = true;
  QString error_message;
  QStringList paths;
  QList<ModelImagePayload> payloads;
};

AdditionalProofImageSet additional_proof_images_for_asset(
    const QVariantMap& asset,
    int max_edge,
    bool details_required,
    const QStringList& required_layer_ids = {}) {
  AdditionalProofImageSet result;
  const auto evidence = asset.value(QStringLiteral("visualEvidence")).toMap();
  const auto has_evidence_list = evidence.contains(QStringLiteral("targetedDetailProofs"));
  const auto paths_value = asset.value(QStringLiteral("additionalProofImagePaths"));
  if (asset.contains(QStringLiteral("additionalProofImagePaths")) &&
      paths_value.metaType().id() != QMetaType::QStringList) {
    result.valid = false;
    result.error_message = QStringLiteral("Targeted detail proof paths must be supplied as a QStringList.");
    return result;
  }
  result.paths = paths_value.toStringList();
  const auto metadata_value = evidence.value(QStringLiteral("targetedDetailProofs"));
  if (has_evidence_list && metadata_value.metaType().id() != QMetaType::QVariantList) {
    result.valid = false;
    result.error_message = QStringLiteral("Targeted detail proof metadata is not a structured list.");
    return result;
  }
  const auto detail_metadata = metadata_value.toList();
  const auto details_expected = details_required || has_evidence_list || !result.paths.isEmpty();
  if (!details_expected) return result;
  if (!has_evidence_list || result.paths.size() != detail_metadata.size() ||
      (details_required && result.paths.isEmpty())) {
    result.valid = false;
    result.error_message = QStringLiteral("Required targeted detail proof images or their metadata are missing.");
    return result;
  }
  if (result.paths.isEmpty()) return result;

  const auto primary_path = asset.value(QStringLiteral("proofImagePath")).toString();
  const auto expected_primary_sha = asset.value(QStringLiteral("planningProofSha256")).toString().toLower();
  if (primary_path.trimmed().isEmpty() || !asset.value(QStringLiteral("planningProofRequired")).toBool() ||
      expected_primary_sha.size() != 64) {
    result.valid = false;
    result.error_message = QStringLiteral("Targeted detail proofs require the pinned hashed overview proof.");
    return result;
  }
  QFile primary_file(primary_path);
  if (!primary_file.open(QIODevice::ReadOnly)) {
    result.valid = false;
    result.error_message = QStringLiteral("The required overview proof for targeted detail crops is unreadable.");
    return result;
  }
  const auto actual_primary_sha = QString::fromLatin1(
      QCryptographicHash::hash(primary_file.readAll(), QCryptographicHash::Sha256).toHex());
  if (actual_primary_sha != expected_primary_sha) {
    result.valid = false;
    result.error_message = QStringLiteral("The overview proof changed after its targeted detail crops were rendered.");
    return result;
  }

  QSet<QString> verified_layer_ids;
  for (qsizetype index = 0; index < result.paths.size(); ++index) {
    const auto path = result.paths.at(index);
    const auto metadata = detail_metadata.at(index).toMap();
    const auto metadata_path = metadata.value(QStringLiteral("imagePath")).toString();
    const auto bounds = metadata.value(QStringLiteral("pixelBounds")).toMap();
    const auto source_sha = metadata.value(QStringLiteral("sourceOverviewSha256")).toString().toLower();
    const auto expected_sha = metadata.value(QStringLiteral("imageSha256")).toString().toLower();
    const auto is_omitted = metadata.contains(QStringLiteral("omitted")) &&
                            metadata.value(QStringLiteral("omitted")).toBool();
    const auto omission_state_known = metadata.contains(QStringLiteral("omitted")) &&
                                      metadata.value(QStringLiteral("omitted")).metaType().id() == QMetaType::Bool;
    const auto layer_id = metadata.value(QStringLiteral("layerId")).toString().trimmed();
    if (path.trimmed().isEmpty() || QFileInfo(path).absoluteFilePath() != QFileInfo(metadata_path).absoluteFilePath() ||
        !metadata.value(QStringLiteral("available")).toBool() || !omission_state_known || is_omitted ||
        source_sha != actual_primary_sha || expected_sha.size() != 64 ||
        layer_id.isEmpty() || verified_layer_ids.contains(layer_id) ||
        bounds.value(QStringLiteral("width")).toInt() <= 0 ||
        bounds.value(QStringLiteral("height")).toInt() <= 0) {
      result.valid = false;
      result.error_message = QStringLiteral("A targeted detail proof is missing, omitted, or not bound to the current overview and layer.");
      result.payloads.clear();
      return result;
    }
    verified_layer_ids.insert(layer_id);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      result.valid = false;
      result.error_message = QStringLiteral("A required targeted detail proof image is unreadable.");
      result.payloads.clear();
      return result;
    }
    const auto bytes = file.readAll();
    const auto actual_sha = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    QImage image;
    if (actual_sha != expected_sha || !image.loadFromData(bytes)) {
      result.valid = false;
      result.error_message = QStringLiteral("A targeted detail proof changed or is not a readable image.");
      result.payloads.clear();
      return result;
    }
    const auto original_size = bytes.size();
    const auto bounded_edge = std::max(1, max_edge);
    if (image.width() > bounded_edge || image.height() > bounded_edge) {
      image = image.scaled(bounded_edge, bounded_edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "JPEG", 84)) {
      result.payloads.append(ModelImagePayload{
          QString::fromLatin1(encoded.toBase64()), path, QStringLiteral("targeted-detail"),
          original_size, encoded.size(), image.size()});
    } else {
      result.valid = false;
      result.error_message = QStringLiteral("A targeted detail proof could not be encoded for local review.");
      result.payloads.clear();
      return result;
    }
  }
  for (const auto& layer_id : required_layer_ids) {
    if (layer_id.trimmed().isEmpty() || !verified_layer_ids.contains(layer_id)) {
      result.valid = false;
      result.error_message = QStringLiteral("A graphic text layer is missing its bound targeted detail proof.");
      result.payloads.clear();
      return result;
    }
  }
  return result;
}

QJsonObject ollama_retouch_actions_schema() {
  return QJsonObject{
      {QStringLiteral("type"), QStringLiteral("array")},
      {QStringLiteral("maxItems"), 2},
      {QStringLiteral("items"),
       QJsonObject{
           {QStringLiteral("type"), QStringLiteral("object")},
           {QStringLiteral("properties"),
            QJsonObject{
                {QStringLiteral("kind"),
                 QJsonObject{
                     {QStringLiteral("type"), QStringLiteral("string")},
                     {QStringLiteral("enum"),
                      QJsonArray{
                          QStringLiteral("blemish"),
                          QStringLiteral("distraction"),
                          QStringLiteral("object"),
                      }},
                 }},
                {QStringLiteral("operation"),
                 QJsonObject{
                     {QStringLiteral("type"), QStringLiteral("string")},
                     {QStringLiteral("enum"),
                      QJsonArray{
                          QStringLiteral("heal"),
                          QStringLiteral("remove"),
                      }},
                 }},
                {QStringLiteral("targetLabel"),
                 QJsonObject{
                     {QStringLiteral("type"), QStringLiteral("string")},
                     {QStringLiteral("maxLength"), 100},
                 }},
                {QStringLiteral("reason"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                {QStringLiteral("confidence"),
                 QJsonObject{
                     {QStringLiteral("type"), QStringLiteral("number")},
                     {QStringLiteral("minimum"), 0.85},
                     {QStringLiteral("maximum"), 1.0},
                 }},
                {QStringLiteral("region"),
                 QJsonObject{
                     {QStringLiteral("type"), QStringLiteral("object")},
                     {QStringLiteral("properties"),
                      QJsonObject{
                          {QStringLiteral("centerX"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                          {QStringLiteral("centerY"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                          {QStringLiteral("radiusX"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.01}, {QStringLiteral("maximum"), 0.12}}},
                          {QStringLiteral("radiusY"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.01}, {QStringLiteral("maximum"), 0.12}}},
                      }},
                     {QStringLiteral("required"),
                      QJsonArray{
                          QStringLiteral("centerX"),
                          QStringLiteral("centerY"),
                          QStringLiteral("radiusX"),
                          QStringLiteral("radiusY"),
                      }},
                 }},
            }},
           {QStringLiteral("required"),
            QJsonArray{
                QStringLiteral("kind"),
                QStringLiteral("operation"),
                QStringLiteral("targetLabel"),
                QStringLiteral("reason"),
                QStringLiteral("confidence"),
                QStringLiteral("region"),
            }},
       }},
  };
}

void add_ollama_decision_variants(
    QJsonObject* schema, bool allow_tool_call_plan,
    bool require_tool_call_plan = false) {
  if (schema == nullptr) return;

  auto properties = schema->value(QStringLiteral("properties")).toObject();
  QJsonArray decisions;
  if (require_tool_call_plan) {
    decisions = QJsonArray{
        QStringLiteral("tool_call_plan"),
        QStringLiteral("needs_user_input"),
        QStringLiteral("cannot_complete_safely"),
    };
  } else {
    decisions = QJsonArray{
        QStringLiteral("edit_intent"),
        QStringLiteral("needs_user_input"),
        QStringLiteral("cannot_complete_safely"),
    };
    if (allow_tool_call_plan)
      decisions.insert(1, QStringLiteral("tool_call_plan"));
  }
  properties.insert(QStringLiteral("decision"), QJsonObject{
      {QStringLiteral("type"), QStringLiteral("string")},
      {QStringLiteral("enum"), decisions},
  });
  auto summary = properties.value(QStringLiteral("summary")).toObject();
  summary.insert(QStringLiteral("type"), QStringLiteral("string"));
  summary.insert(QStringLiteral("minLength"), 1);
  summary.insert(QStringLiteral("maxLength"), 320);
  properties.insert(QStringLiteral("summary"), summary);

  auto citations = properties.value(QStringLiteral("styleConstraintCitations")).toObject();
  citations.insert(QStringLiteral("type"), QStringLiteral("array"));
  citations.insert(QStringLiteral("minItems"), 1);
  if (citations.value(QStringLiteral("items")).toObject().isEmpty()) {
    citations.insert(QStringLiteral("items"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), QJsonObject{
            {QStringLiteral("field"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
            {QStringLiteral("reason"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
        }},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("field"), QStringLiteral("reason")}},
    });
  }
  properties.insert(QStringLiteral("styleConstraintCitations"), citations);

  auto edit_intent = properties.value(QStringLiteral("editIntent")).toObject();
  edit_intent.insert(QStringLiteral("type"), QStringLiteral("object"));
  if (edit_intent.value(QStringLiteral("properties")).toObject().isEmpty()) {
    edit_intent.insert(QStringLiteral("properties"), QJsonObject{
        {QStringLiteral("goalSummary"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("minLength"), 1},
        }},
        {QStringLiteral("confidence"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
        {QStringLiteral("globalChangePolicy"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("enum"), QJsonArray{QStringLiteral("allow"), QStringLiteral("preserve")}},
        }},
        {QStringLiteral("visualFindings"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("array")},
            {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
        }},
        {QStringLiteral("localizedActions"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("array")},
            {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
        }},
        {QStringLiteral("retouchActions"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("array")},
            {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
        }},
    });
  }
  auto intent_properties = edit_intent.value(QStringLiteral("properties")).toObject();
  const QJsonObject photo_intent_controls{
      {QStringLiteral("completionStatus"), QJsonObject{
          {QStringLiteral("type"), QStringLiteral("string")},
          {QStringLiteral("enum"), QJsonArray{
              QStringLiteral("in_progress"), QStringLiteral("complete")}},
          {QStringLiteral("description"), QStringLiteral(
              "Declare complete only when all approved requested work is finished and verified; otherwise use in_progress.")},
      }},
      {QStringLiteral("colorMode"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
          {QStringLiteral("enum"), QJsonArray{QStringLiteral("preserve"), QStringLiteral("color"), QStringLiteral("monochrome")}}}},
      {QStringLiteral("contrast"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
          {QStringLiteral("enum"), QJsonArray{QStringLiteral("preserve"), QStringLiteral("soft"), QStringLiteral("natural"), QStringLiteral("strong")}}}},
      {QStringLiteral("brightness"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
          {QStringLiteral("enum"), QJsonArray{QStringLiteral("preserve"), QStringLiteral("darker"), QStringLiteral("brighter")}}}},
      {QStringLiteral("palette"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
          {QStringLiteral("enum"), QJsonArray{QStringLiteral("preserve"), QStringLiteral("reference"), QStringLiteral("warm"), QStringLiteral("cool"), QStringLiteral("neutral")}}}},
      {QStringLiteral("saturation"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
          {QStringLiteral("enum"), QJsonArray{QStringLiteral("preserve"), QStringLiteral("less"), QStringLiteral("more")}}}},
      {QStringLiteral("crop"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
          {QStringLiteral("enum"), QJsonArray{QStringLiteral("preserve"), QStringLiteral("improve"), QStringLiteral("tighter")}}}},
      {QStringLiteral("detail"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
          {QStringLiteral("enum"), QJsonArray{QStringLiteral("preserve"), QStringLiteral("soft"), QStringLiteral("natural"), QStringLiteral("crisp")}}}},
      {QStringLiteral("mood"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
          {QStringLiteral("enum"), QJsonArray{QStringLiteral("reference"), QStringLiteral("natural"), QStringLiteral("editorial"), QStringLiteral("clean"), QStringLiteral("moody")}}}},
      {QStringLiteral("referenceStrength"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
          {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
      {QStringLiteral("requestedActions"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
          {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
      {QStringLiteral("requiresApproval"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
          {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
  };
  if (allow_tool_call_plan) {
    for (auto it = photo_intent_controls.constBegin(); it != photo_intent_controls.constEnd(); ++it) {
      if (!intent_properties.contains(it.key())) intent_properties.insert(it.key(), it.value());
    }
  }
  auto goal_summary = intent_properties.value(QStringLiteral("goalSummary")).toObject();
  goal_summary.insert(QStringLiteral("type"), QStringLiteral("string"));
  goal_summary.insert(QStringLiteral("minLength"), 1);
  intent_properties.insert(QStringLiteral("goalSummary"), goal_summary);
  edit_intent.insert(QStringLiteral("properties"), intent_properties);
  edit_intent.insert(QStringLiteral("required"), QJsonArray{
      QStringLiteral("goalSummary"), QStringLiteral("confidence"),
      QStringLiteral("globalChangePolicy"), QStringLiteral("visualFindings"),
      QStringLiteral("localizedActions"), QStringLiteral("retouchActions"),
  });
  if (allow_tool_call_plan) {
    auto edit_intent_required = edit_intent.value(QStringLiteral("required")).toArray();
    if (!edit_intent_required.contains(QStringLiteral("completionStatus")))
      edit_intent_required.append(QStringLiteral("completionStatus"));
    edit_intent.insert(QStringLiteral("required"), edit_intent_required);
  }
  properties.insert(QStringLiteral("editIntent"), edit_intent);

  auto tool_calls = properties.value(QStringLiteral("toolCalls")).toObject();
  tool_calls.insert(QStringLiteral("type"), QStringLiteral("array"));
  tool_calls.insert(QStringLiteral("minItems"), 1);
  properties.insert(QStringLiteral("toolCalls"), tool_calls);

  properties.insert(QStringLiteral("userQuestions"), QJsonObject{
      {QStringLiteral("type"), QStringLiteral("array")},
      {QStringLiteral("minItems"), 1},
      {QStringLiteral("items"), QJsonObject{
          {QStringLiteral("type"), QStringLiteral("string")},
          {QStringLiteral("minLength"), 1},
      }},
  });
  properties.insert(QStringLiteral("reason"), QJsonObject{
      {QStringLiteral("type"), QStringLiteral("string")},
      {QStringLiteral("minLength"), 1},
  });
  schema->insert(QStringLiteral("properties"), properties);
  schema->insert(QStringLiteral("required"), QJsonArray{
      QStringLiteral("decision"), QStringLiteral("summary"),
  });

  const auto decision_variant = [&properties](const QString& decision, const QJsonArray& payload_required) {
    QJsonObject branch_properties{
        {QStringLiteral("decision"), properties.value(QStringLiteral("decision"))},
        {QStringLiteral("summary"), properties.value(QStringLiteral("summary"))},
    };
    QJsonArray required{QStringLiteral("decision"), QStringLiteral("summary")};
    for (const auto& field_value : payload_required) {
      const auto field = field_value.toString();
      if (properties.contains(field)) branch_properties.insert(field, properties.value(field));
      required.append(field);
    }
    auto branch_decision = branch_properties.value(QStringLiteral("decision")).toObject();
    branch_decision.insert(QStringLiteral("enum"), QJsonArray{decision});
    branch_properties.insert(QStringLiteral("decision"), branch_decision);
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), branch_properties},
        {QStringLiteral("required"), required},
    };
  };
  QJsonArray variants;
  if (!require_tool_call_plan) {
    variants.append(decision_variant(QStringLiteral("edit_intent"), QJsonArray{
        QStringLiteral("styleConstraintCitations"), QStringLiteral("editIntent"),
    }));
  }
  if (allow_tool_call_plan) {
    variants.append(decision_variant(QStringLiteral("tool_call_plan"), QJsonArray{
        QStringLiteral("toolCalls"),
    }));
  }
  variants.append(decision_variant(QStringLiteral("needs_user_input"), QJsonArray{
      QStringLiteral("userQuestions"),
  }));
  variants.append(decision_variant(QStringLiteral("cannot_complete_safely"), QJsonArray{
      QStringLiteral("reason"),
  }));
  schema->insert(QStringLiteral("oneOf"), variants);
}

QJsonObject ollama_response_payload_schema(
    bool allow_tool_call_plan, bool require_tool_call_plan = false) {
  const QJsonObject tool_call_item{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("properties"), QJsonObject{
          {QStringLiteral("name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
          {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
          {QStringLiteral("reason"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
      }},
      {QStringLiteral("required"), QJsonArray{QStringLiteral("name"), QStringLiteral("arguments")}},
  };
  QJsonObject properties{
      {QStringLiteral("decision"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
      {QStringLiteral("summary"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
      {QStringLiteral("styleConstraintCitations"),
       QJsonObject{
           {QStringLiteral("type"), QStringLiteral("array")},
       }},
      {QStringLiteral("editIntent"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
      {QStringLiteral("toolCalls"), QJsonObject{
          {QStringLiteral("type"), QStringLiteral("array")},
          {QStringLiteral("items"), tool_call_item},
      }},
      {QStringLiteral("reason"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
      {QStringLiteral("userQuestions"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
      {QStringLiteral("warnings"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
  };
  QJsonArray required{
      QStringLiteral("decision"),
      QStringLiteral("summary"),
  };
  if (!allow_tool_call_plan) {
    properties.insert(
        QStringLiteral("decision"),
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("enum"),
             QJsonArray{
                 QStringLiteral("edit_intent"),
                 QStringLiteral("needs_user_input"),
                 QStringLiteral("cannot_complete_safely"),
             }},
        });
    properties.insert(
        QStringLiteral("styleConstraintCitations"),
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("array")},
            {QStringLiteral("items"),
             QJsonObject{
                 {QStringLiteral("type"), QStringLiteral("object")},
                 {QStringLiteral("properties"),
                  QJsonObject{
                      {QStringLiteral("field"),
                       QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                      {QStringLiteral("reason"),
                       QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                  }},
                 {QStringLiteral("required"),
                  QJsonArray{QStringLiteral("field"), QStringLiteral("reason")}},
             }},
        });
    properties.insert(
        QStringLiteral("editIntent"),
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"),
             QJsonObject{
                 {QStringLiteral("goalSummary"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                 {QStringLiteral("colorMode"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("enum"), QJsonArray{QStringLiteral("preserve"), QStringLiteral("color"), QStringLiteral("monochrome")}}}},
                 {QStringLiteral("contrast"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("enum"), QJsonArray{QStringLiteral("preserve"), QStringLiteral("soft"), QStringLiteral("natural"), QStringLiteral("strong")}}}},
                 {QStringLiteral("brightness"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("enum"), QJsonArray{QStringLiteral("preserve"), QStringLiteral("darker"), QStringLiteral("brighter")}}}},
                 {QStringLiteral("palette"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("enum"), QJsonArray{QStringLiteral("preserve"), QStringLiteral("reference"), QStringLiteral("warm"), QStringLiteral("cool"), QStringLiteral("neutral")}}}},
                 {QStringLiteral("saturation"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("enum"), QJsonArray{QStringLiteral("preserve"), QStringLiteral("less"), QStringLiteral("more")}}}},
                 {QStringLiteral("crop"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("enum"), QJsonArray{QStringLiteral("preserve"), QStringLiteral("improve"), QStringLiteral("tighter")}}}},
                 {QStringLiteral("detail"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("enum"), QJsonArray{QStringLiteral("preserve"), QStringLiteral("soft"), QStringLiteral("natural"), QStringLiteral("crisp")}}}},
                 {QStringLiteral("mood"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("enum"), QJsonArray{QStringLiteral("reference"), QStringLiteral("natural"), QStringLiteral("editorial"), QStringLiteral("clean"), QStringLiteral("moody")}}}},
                 {QStringLiteral("referenceStrength"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
                 {QStringLiteral("requestedActions"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
                 {QStringLiteral("requiresApproval"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
                 {QStringLiteral("confidence"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
                 {QStringLiteral("globalChangePolicy"),
                  QJsonObject{
                      {QStringLiteral("type"), QStringLiteral("string")},
                      {QStringLiteral("enum"),
                       QJsonArray{
                           QStringLiteral("allow"),
                           QStringLiteral("preserve"),
                       }},
                  }},
                 {QStringLiteral("visualFindings"),
                  QJsonObject{
                      {QStringLiteral("type"), QStringLiteral("array")},
                      {QStringLiteral("maxItems"), 4},
                      {QStringLiteral("items"),
                       QJsonObject{
                           {QStringLiteral("type"), QStringLiteral("object")},
                           {QStringLiteral("properties"),
                            QJsonObject{
                                {QStringLiteral("kind"),
                                 QJsonObject{
                                     {QStringLiteral("type"), QStringLiteral("string")},
                                     {QStringLiteral("enum"),
                                      QJsonArray{
                                          QStringLiteral("exposure"),
                                          QStringLiteral("highlight_clipping"),
                                          QStringLiteral("shadow_crush"),
                                          QStringLiteral("color_cast"),
                                          QStringLiteral("skin_tone"),
                                          QStringLiteral("subject_separation"),
                                          QStringLiteral("background_distraction"),
                                          QStringLiteral("composition"),
                                          QStringLiteral("detail"),
                                      }},
                                 }},
                                {QStringLiteral("target"),
                                 QJsonObject{
                                     {QStringLiteral("type"), QStringLiteral("string")},
                                     {QStringLiteral("enum"),
                                      QJsonArray{
                                          QStringLiteral("whole_image"),
                                          QStringLiteral("subject"),
                                          QStringLiteral("face"),
                                          QStringLiteral("skin"),
                                          QStringLiteral("background"),
                                          QStringLiteral("sky"),
                                          QStringLiteral("foreground"),
                                          QStringLiteral("object"),
                                      }},
                                 }},
                                {QStringLiteral("targetLabel"),
                                 QJsonObject{
                                     {QStringLiteral("type"), QStringLiteral("string")},
                                     {QStringLiteral("maxLength"), 100},
                                 }},
                                {QStringLiteral("severity"),
                                 QJsonObject{
                                     {QStringLiteral("type"), QStringLiteral("string")},
                                     {QStringLiteral("enum"),
                                      QJsonArray{
                                          QStringLiteral("low"),
                                          QStringLiteral("medium"),
                                          QStringLiteral("high"),
                                      }},
                                 }},
                                {QStringLiteral("observation"),
                                 QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                {QStringLiteral("confidence"),
                                 QJsonObject{
                                     {QStringLiteral("type"), QStringLiteral("number")},
                                     {QStringLiteral("minimum"), 0.0},
                                     {QStringLiteral("maximum"), 1.0},
                                 }},
                                {QStringLiteral("region"),
                                 QJsonObject{
                                     {QStringLiteral("type"), QStringLiteral("object")},
                                     {QStringLiteral("properties"),
                                      QJsonObject{
                                          {QStringLiteral("centerX"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                          {QStringLiteral("centerY"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                          {QStringLiteral("radiusX"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.04}, {QStringLiteral("maximum"), 0.5}}},
                                          {QStringLiteral("radiusY"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.04}, {QStringLiteral("maximum"), 0.5}}},
                                      }},
                                     {QStringLiteral("required"), QJsonArray{QStringLiteral("centerX"), QStringLiteral("centerY"), QStringLiteral("radiusX"), QStringLiteral("radiusY")}},
                                 }},
                            }},
                           {QStringLiteral("required"),
                            QJsonArray{
                                QStringLiteral("kind"),
                                QStringLiteral("target"),
                                QStringLiteral("severity"),
                                QStringLiteral("observation"),
                                QStringLiteral("confidence"),
                                QStringLiteral("region"),
                            }},
                       }},
                  }},
                 {QStringLiteral("localizedActions"),
                  QJsonObject{
                      {QStringLiteral("type"), QStringLiteral("array")},
                      {QStringLiteral("maxItems"), 2},
                      {QStringLiteral("items"),
                       QJsonObject{
                           {QStringLiteral("type"), QStringLiteral("object")},
                           {QStringLiteral("properties"),
                            QJsonObject{
                                {QStringLiteral("target"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("subject"), QStringLiteral("face"), QStringLiteral("skin"), QStringLiteral("background"), QStringLiteral("sky"), QStringLiteral("foreground"), QStringLiteral("object")}}}},
                                {QStringLiteral("targetLabel"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 100}}},
                                {QStringLiteral("maskShape"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("radial")}}}},
                                {QStringLiteral("selectionMode"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("region"), QStringLiteral("semantic"), QStringLiteral("guided")}}}},
                                {QStringLiteral("selectionCandidates"),
                                 QJsonObject{
                                     {QStringLiteral("type"), QStringLiteral("array")},
                                     {QStringLiteral("minItems"), 2},
                                     {QStringLiteral("maxItems"), 3},
                                     {QStringLiteral("items"),
                                      QJsonObject{
                                          {QStringLiteral("type"), QStringLiteral("object")},
                                          {QStringLiteral("properties"),
                                           QJsonObject{
                                               {QStringLiteral("label"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 100}}},
                                               {QStringLiteral("confidence"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                               {QStringLiteral("region"),
                                                QJsonObject{
                                                    {QStringLiteral("type"), QStringLiteral("object")},
                                                    {QStringLiteral("properties"),
                                                     QJsonObject{
                                                         {QStringLiteral("centerX"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                                         {QStringLiteral("centerY"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                                         {QStringLiteral("radiusX"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.04}, {QStringLiteral("maximum"), 0.5}}},
                                                         {QStringLiteral("radiusY"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.04}, {QStringLiteral("maximum"), 0.5}}},
                                                         {QStringLiteral("feather"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.05}, {QStringLiteral("maximum"), 1.0}}},
                                                     }},
                                                    {QStringLiteral("required"), QJsonArray{QStringLiteral("centerX"), QStringLiteral("centerY"), QStringLiteral("radiusX"), QStringLiteral("radiusY"), QStringLiteral("feather")}},
                                                }},
                                           }},
                                          {QStringLiteral("required"), QJsonArray{QStringLiteral("label"), QStringLiteral("confidence"), QStringLiteral("region")}},
                                      }},
                                 }},
                                {QStringLiteral("reason"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                {QStringLiteral("confidence"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.6}, {QStringLiteral("maximum"), 1.0}}},
                                {QStringLiteral("region"),
                                 QJsonObject{
                                     {QStringLiteral("type"), QStringLiteral("object")},
                                     {QStringLiteral("properties"),
                                      QJsonObject{
                                          {QStringLiteral("centerX"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                          {QStringLiteral("centerY"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
                                          {QStringLiteral("radiusX"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.04}, {QStringLiteral("maximum"), 0.5}}},
                                          {QStringLiteral("radiusY"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.04}, {QStringLiteral("maximum"), 0.5}}},
                                          {QStringLiteral("feather"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.05}, {QStringLiteral("maximum"), 1.0}}},
                                      }},
                                     {QStringLiteral("required"), QJsonArray{QStringLiteral("centerX"), QStringLiteral("centerY"), QStringLiteral("radiusX"), QStringLiteral("radiusY"), QStringLiteral("feather")}},
                                 }},
                                {QStringLiteral("adjustments"),
                                 QJsonObject{
                                     {QStringLiteral("type"), QStringLiteral("object")},
                                     {QStringLiteral("properties"),
                                      QJsonObject{
                                          {QStringLiteral("exposure"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
                                          {QStringLiteral("contrast"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
                                          {QStringLiteral("highlights"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
                                          {QStringLiteral("shadows"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
                                          {QStringLiteral("temperature"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
                                          {QStringLiteral("tint"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
                                          {QStringLiteral("vibrance"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
                                          {QStringLiteral("saturation"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
                                          {QStringLiteral("texture"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
                                          {QStringLiteral("clarity"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
                                          {QStringLiteral("dehaze"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
                                      }},
                                 }},
                            }},
                           {QStringLiteral("required"),
                            QJsonArray{
                                QStringLiteral("target"),
                                QStringLiteral("maskShape"),
                                QStringLiteral("selectionMode"),
                                QStringLiteral("reason"),
                                QStringLiteral("confidence"),
                                QStringLiteral("region"),
                                QStringLiteral("adjustments"),
                            }},
                       }},
                  }},
                 {QStringLiteral("retouchActions"),
                  ollama_retouch_actions_schema()},
             }},
            {QStringLiteral("required"),
             QJsonArray{
                 QStringLiteral("goalSummary"),
                 QStringLiteral("confidence"),
                 QStringLiteral("globalChangePolicy"),
                 QStringLiteral("visualFindings"),
                 QStringLiteral("localizedActions"),
                 QStringLiteral("retouchActions"),
             }},
        });
    required.append(QStringLiteral("styleConstraintCitations"));
    required.append(QStringLiteral("editIntent"));
  }
  QJsonObject schema{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("properties"), properties},
      {QStringLiteral("required"), required},
  };
  add_ollama_decision_variants(
      &schema, allow_tool_call_plan, require_tool_call_plan);
  return schema;
}

// A read request occasionally arrives under the edit tool's name. Correct only
// this unambiguous alias, and only when inspection is in the supplied tool set.
// Keep malformed requests intact so ordinary validation can reject them.
QVariantList normalize_composition_inspection_calls(
    const QVariantList& calls, const QVariantList& tools, QVariantList* corrections) {
  const auto can_inspect = std::any_of(tools.cbegin(), tools.cend(), [](const auto& value) {
    return value.toMap().value(QStringLiteral("name")).toString() ==
        QStringLiteral("photo.inspect_composition");
  });
  if (!can_inspect) return calls;
  auto normalized = calls;
  const QStringList allowed_keys{
      QStringLiteral("assetId"), QStringLiteral("operation"), QStringLiteral("values")};
  for (qsizetype i = 0; i < normalized.size(); ++i) {
    auto call = normalized.at(i).toMap();
    auto arguments = call.value(QStringLiteral("arguments")).toMap();
    if (call.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        arguments.value(QStringLiteral("operation")).toString() != QStringLiteral("inspect") ||
        arguments.value(QStringLiteral("assetId")).metaType().id() != QMetaType::QString ||
        arguments.value(QStringLiteral("assetId")).toString().trimmed().isEmpty() ||
        std::any_of(arguments.keyBegin(), arguments.keyEnd(), [&allowed_keys](const auto& key) {
          return !allowed_keys.contains(key);
        })) continue;
    if (arguments.contains(QStringLiteral("values"))) {
      if (arguments.value(QStringLiteral("values")).metaType().id() != QMetaType::QVariantMap) continue;
      const auto values = arguments.value(QStringLiteral("values")).toMap();
      if (!values.isEmpty()) {
        const auto family = values.value(QStringLiteral("family"));
        const auto face = values.value(QStringLiteral("face"));
        if (values.size() > 2 || family.metaType().id() != QMetaType::QString ||
            family.toString().trimmed().isEmpty() ||
            (values.contains(QStringLiteral("face")) &&
             (face.metaType().id() != QMetaType::QString || face.toString().trimmed().isEmpty())) ||
            std::any_of(values.keyBegin(), values.keyEnd(), [](const auto& key) {
              return key != QStringLiteral("family") && key != QStringLiteral("face");
            })) continue;
      } else {
        arguments.remove(QStringLiteral("values"));
      }
    }
    arguments.remove(QStringLiteral("operation"));
    call.insert(QStringLiteral("name"), QStringLiteral("photo.inspect_composition"));
    call.insert(QStringLiteral("arguments"), arguments);
    normalized[i] = call;
    corrections->append(QVariantMap{{QStringLiteral("index"), i},
        {QStringLiteral("requestedCall"), calls.at(i)}, {QStringLiteral("executedCall"), call},
        {QStringLiteral("reason"), QStringLiteral("Composition inspection uses the read-only tool.")}});
  }
  return normalized;
}

QVariantMap prior_native_composition_completion_evidence(
    const AgentLocalModelPlanRequest& request) {
  const auto asset_id = request.asset.value(QStringLiteral("assetId")).toString().trimmed();
  if (asset_id.isEmpty()) return {};

  static const QSet<QString> localized_operations{
      QStringLiteral("add_text"), QStringLiteral("text"), QStringLiteral("typography"),
      QStringLiteral("text_path"), QStringLiteral("transform"), QStringLiteral("perspective"),
      QStringLiteral("move"), QStringLiteral("copy_selection"), QStringLiteral("erase_selection"),
      QStringLiteral("select_subject"), QStringLiteral("lasso_selection"), QStringLiteral("blur"),
      QStringLiteral("opacity"), QStringLiteral("paint"), QStringLiteral("add_effect"),
      QStringLiteral("update_effect"), QStringLiteral("remove_effect"),
      QStringLiteral("reorder_effect"), QStringLiteral("mask_effect"),
      QStringLiteral("clear_effect_mask"),
  };
  QSet<QString> mutated_layer_ids;
  QSet<QString> mutated_mask_ids;
  QStringList operation_names;
  auto accepted_mutation_count = 0;
  auto readback_confirmed = false;

  for (const auto& prior_value : request.prior_tool_results) {
    const auto prior = prior_value.toMap();
    const auto tool_name = prior.value(QStringLiteral("toolName"),
        prior.value(QStringLiteral("name"))).toString();
    auto result = prior.value(QStringLiteral("result")).toMap();
    if (result.isEmpty()) result = prior;
    const auto arguments = prior.value(QStringLiteral("arguments")).toMap();
    if (!result.value(QStringLiteral("ok")).toBool() ||
        arguments.value(QStringLiteral("assetId")).toString().trimmed() != asset_id)
      continue;

    auto content = result.value(QStringLiteral("structuredContent")).toMap();
    if (content.isEmpty()) {
      content = result.value(QStringLiteral("toolResult")).toMap()
          .value(QStringLiteral("structuredContent")).toMap();
    }
    if (tool_name == QStringLiteral("photo.edit_composition")) {
      const auto operation = arguments.value(QStringLiteral("operation")).toString();
      if (!localized_operations.contains(operation)) continue;
      const auto created_layer_id = content.value(QStringLiteral("createdLayerId")).toString().trimmed();
      const auto created_layer_ids = content.value(QStringLiteral("createdLayerIds")).toStringList();
      const auto created_mask_id = content.value(QStringLiteral("createdMaskId")).toString().trimmed();
      const auto created_mask_ids = content.value(QStringLiteral("createdMaskIds")).toStringList();
      const auto layer_id = arguments.value(QStringLiteral("layerId")).toString().trimmed();
      const auto mask_id = arguments.value(QStringLiteral("maskId")).toString().trimmed();
      const auto changed = content.value(QStringLiteral("changed")).toBool();
      if (!changed && created_layer_id.isEmpty() && created_layer_ids.isEmpty() &&
          created_mask_id.isEmpty() && created_mask_ids.isEmpty())
        continue;

      ++accepted_mutation_count;
      if (!operation_names.contains(operation)) operation_names.append(operation);
      if (!layer_id.isEmpty()) mutated_layer_ids.insert(layer_id);
      if (!created_layer_id.isEmpty()) mutated_layer_ids.insert(created_layer_id);
      for (const auto& id : created_layer_ids)
        if (!id.trimmed().isEmpty()) mutated_layer_ids.insert(id.trimmed());
      if (!mask_id.isEmpty()) mutated_mask_ids.insert(mask_id);
      if (!created_mask_id.isEmpty()) mutated_mask_ids.insert(created_mask_id);
      for (const auto& id : created_mask_ids)
        if (!id.trimmed().isEmpty()) mutated_mask_ids.insert(id.trimmed());
      readback_confirmed = false;
      continue;
    }

    if (tool_name != QStringLiteral("photo.inspect_composition") ||
        accepted_mutation_count == 0)
      continue;
    QSet<QString> inspected_layer_ids;
    for (const auto& layer_value : content.value(QStringLiteral("layers")).toList()) {
      const auto id = layer_value.toMap().value(QStringLiteral("id")).toString().trimmed();
      if (!id.isEmpty()) inspected_layer_ids.insert(id);
    }
    QSet<QString> inspected_mask_ids;
    for (const auto& mask_value : content.value(QStringLiteral("masks")).toList()) {
      const auto id = mask_value.toMap().value(QStringLiteral("id")).toString().trimmed();
      if (!id.isEmpty()) inspected_mask_ids.insert(id);
    }
    const auto layers_match = std::all_of(mutated_layer_ids.cbegin(), mutated_layer_ids.cend(),
        [&inspected_layer_ids](const QString& id) { return inspected_layer_ids.contains(id); });
    const auto masks_match = std::all_of(mutated_mask_ids.cbegin(), mutated_mask_ids.cend(),
        [&inspected_mask_ids](const QString& id) { return inspected_mask_ids.contains(id); });
    readback_confirmed = (!mutated_layer_ids.isEmpty() || !mutated_mask_ids.isEmpty()) &&
        layers_match && masks_match;
  }

  if (accepted_mutation_count == 0 || operation_names.isEmpty() || !readback_confirmed)
    return {};
  return QVariantMap{
      {QStringLiteral("requested"), true},
      {QStringLiteral("acceptedMutationCount"), accepted_mutation_count},
      {QStringLiteral("operationNames"), operation_names},
      {QStringLiteral("assetId"), asset_id},
      {QStringLiteral("readbackConfirmed"), true},
  };
}

bool edit_intent_keeps_global_controls_neutral(const QVariantMap& intent) {
  const auto color = intent.value(QStringLiteral("colorMode")).toString();
  const auto contrast = intent.value(QStringLiteral("contrast")).toString();
  const auto detail = intent.value(QStringLiteral("detail")).toString();
  return (color == QStringLiteral("preserve") || color == QStringLiteral("color")) &&
      (contrast == QStringLiteral("preserve") || contrast == QStringLiteral("natural")) &&
      intent.value(QStringLiteral("brightness")).toString() == QStringLiteral("preserve") &&
      intent.value(QStringLiteral("palette")).toString() == QStringLiteral("preserve") &&
      intent.value(QStringLiteral("saturation")).toString() == QStringLiteral("preserve") &&
      intent.value(QStringLiteral("crop")).toString() == QStringLiteral("preserve") &&
      (detail == QStringLiteral("preserve") || detail == QStringLiteral("natural")) &&
      intent.value(QStringLiteral("mood")).toString() == QStringLiteral("natural") &&
      intent.value(QStringLiteral("referenceStrength")).toDouble() <= 0.0 &&
      intent.value(QStringLiteral("requestedActions")).toStringList().isEmpty() &&
      intent.value(QStringLiteral("requiresApproval")).toList().isEmpty();
}

bool json_schema_operation_matches(const QJsonObject& condition, const QString& operation) {
  const auto operation_schema = condition.value(QStringLiteral("properties")).toObject()
      .value(QStringLiteral("operation")).toObject();
  const auto constant = operation_schema.value(QStringLiteral("const"));
  if (!constant.isUndefined()) return constant.toString() == operation;
  const auto enum_values = operation_schema.value(QStringLiteral("enum")).toArray();
  if (!enum_values.isEmpty()) return enum_values.contains(operation);
  return false;
}

bool json_schema_condition_targets_operation(const QJsonObject& condition) {
  const auto operation_schema = condition.value(QStringLiteral("properties")).toObject()
      .value(QStringLiteral("operation")).toObject();
  return operation_schema.contains(QStringLiteral("const")) ||
      operation_schema.value(QStringLiteral("enum")).isArray();
}

QJsonObject merge_json_schema_constraints(QJsonObject base, const QJsonObject& constraint) {
  if (constraint.value(QStringLiteral("type")).toString() == QStringLiteral("object") &&
      constraint.value(QStringLiteral("additionalProperties")).isBool() &&
      !constraint.value(QStringLiteral("additionalProperties")).toBool() &&
      constraint.value(QStringLiteral("properties")).isObject()) {
    // A strict object schema is complete. Keeping the broad base properties
    // would accidentally make union-only fields valid in this operation.
    return constraint;
  }
  auto properties = base.value(QStringLiteral("properties")).toObject();
  const auto constraint_properties = constraint.value(QStringLiteral("properties")).toObject();
  for (auto it = constraint_properties.begin(); it != constraint_properties.end(); ++it) {
    const auto existing = properties.value(it.key());
    const auto addition = it.value();
    if (existing.isObject() && addition.isObject()) {
      properties.insert(it.key(), merge_json_schema_constraints(existing.toObject(), addition.toObject()));
    } else {
      properties.insert(it.key(), addition);
    }
  }
  if (!constraint_properties.isEmpty() || base.contains(QStringLiteral("properties")))
    base.insert(QStringLiteral("properties"), properties);
  for (auto it = constraint.begin(); it != constraint.end(); ++it) {
    if (it.key() == QStringLiteral("properties")) continue;
    if (it.key() == QStringLiteral("required")) {
      auto required = base.value(QStringLiteral("required")).toArray();
      for (const auto& item : it.value().toArray())
        if (!required.contains(item)) required.append(item);
      base.insert(QStringLiteral("required"), required);
    } else if (it.key() == QStringLiteral("allOf")) {
      auto all_of = base.value(QStringLiteral("allOf")).toArray();
      for (const auto& item : it.value().toArray()) all_of.append(item);
      base.insert(QStringLiteral("allOf"), all_of);
    } else {
      base.insert(it.key(), it.value());
    }
  }
  return base;
}

QJsonObject grammar_compatible_json_schema(const QJsonObject& source) {
  QJsonObject schema = source;
  // Ollama's decoder consumes the structural constraints below; verbose JSON
  // Schema annotations are repeated in the human-readable tool catalog and
  // can dominate the context window. Keep the canonical schemas untouched for
  // host validation and remove only non-validating annotations from this
  // generation-only copy.
  for (const auto& key : {QStringLiteral("description"), QStringLiteral("title"),
           QStringLiteral("examples"), QStringLiteral("default"), QStringLiteral("$comment")})
    schema.remove(key);
  auto properties = schema.value(QStringLiteral("properties")).toObject();
  for (auto it = properties.begin(); it != properties.end(); ++it)
    if (it.value().isObject())
      properties.insert(it.key(), grammar_compatible_json_schema(it.value().toObject()));
  if (!properties.isEmpty() || schema.contains(QStringLiteral("properties")))
    schema.insert(QStringLiteral("properties"), properties);

  for (const auto& key : {QStringLiteral("items"), QStringLiteral("additionalProperties"),
           QStringLiteral("if"), QStringLiteral("then"), QStringLiteral("else"),
           QStringLiteral("not")}) {
    const auto nested = schema.value(key);
    if (nested.isObject())
      schema.insert(key, grammar_compatible_json_schema(nested.toObject()));
  }
  for (const auto& key : {QStringLiteral("allOf"), QStringLiteral("oneOf"),
           QStringLiteral("anyOf")}) {
    auto branches = schema.value(key).toArray();
    for (qsizetype index = 0; index < branches.size(); ++index)
      if (branches.at(index).isObject())
        branches.replace(index, grammar_compatible_json_schema(branches.at(index).toObject()));
    if (!branches.isEmpty()) schema.insert(key, branches);
  }

  const auto union_key = schema.contains(QStringLiteral("oneOf"))
      ? QStringLiteral("oneOf")
      : schema.contains(QStringLiteral("anyOf")) ? QStringLiteral("anyOf") : QString{};
  if (union_key.isEmpty() || !schema.contains(QStringLiteral("properties"))) return schema;

  auto shared = schema;
  shared.remove(QStringLiteral("oneOf"));
  shared.remove(QStringLiteral("anyOf"));
  const auto union_branches = schema.value(union_key).toArray();
  QJsonArray normalized_branches;
  for (const auto& branch_value : union_branches) {
    if (!branch_value.isObject()) continue;
    auto branch = merge_json_schema_constraints(shared, branch_value.toObject());
    for (const auto& required_value : shared.value(QStringLiteral("required")).toArray()) {
      auto required = branch.value(QStringLiteral("required")).toArray();
      if (!required.contains(required_value)) required.append(required_value);
      branch.insert(QStringLiteral("required"), required);
    }
    if (branch_value.toObject().value(QStringLiteral("additionalProperties")).isBool() &&
        !branch_value.toObject().value(QStringLiteral("additionalProperties")).toBool()) {
      for (auto it = shared.constBegin(); it != shared.constEnd(); ++it) {
        if (it.key() == QStringLiteral("properties") ||
            it.key() == QStringLiteral("required") ||
            it.key() == QStringLiteral("oneOf") ||
            it.key() == QStringLiteral("anyOf"))
          continue;
        if (!branch.contains(it.key())) branch.insert(it.key(), it.value());
      }
    }
    normalized_branches.append(grammar_compatible_json_schema(branch));
  }
  if (normalized_branches.isEmpty()) return schema;

  QJsonObject normalized;
  for (const auto& key : {QStringLiteral("$schema"), QStringLiteral("$defs"),
           QStringLiteral("definitions"), QStringLiteral("description"),
           QStringLiteral("title")})
    if (schema.contains(key)) normalized.insert(key, schema.value(key));
  normalized.insert(union_key, normalized_branches);
  return normalized;
}

QJsonValue json_schema_without_annotations(const QJsonValue& value,
    bool preserve_descriptions = false);

QJsonValue json_schema_without_annotations(const QJsonValue& value,
    bool preserve_descriptions) {
  if (value.isArray()) {
    QJsonArray result;
    for (const auto& item : value.toArray())
      result.append(json_schema_without_annotations(item, preserve_descriptions));
    return result;
  }
  if (!value.isObject()) return value;
  QJsonObject result;
  const auto object = value.toObject();
  const QSet<QString> annotations{
      QStringLiteral("description"), QStringLiteral("title"), QStringLiteral("examples"),
      QStringLiteral("default"), QStringLiteral("$comment")};
  const QSet<QString> schema_maps{
      QStringLiteral("properties"), QStringLiteral("patternProperties"),
      QStringLiteral("$defs"), QStringLiteral("definitions")};
  const QSet<QString> schema_arrays{
      QStringLiteral("allOf"), QStringLiteral("anyOf"), QStringLiteral("oneOf")};
  const QSet<QString> schema_children{
      QStringLiteral("items"), QStringLiteral("additionalProperties"),
      QStringLiteral("unevaluatedProperties"), QStringLiteral("unevaluatedItems"),
      QStringLiteral("contains"), QStringLiteral("not"), QStringLiteral("if"),
      QStringLiteral("then"), QStringLiteral("else"), QStringLiteral("propertyNames"),
      QStringLiteral("contentSchema")};
  for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
    if (annotations.contains(it.key()) &&
        !(preserve_descriptions && it.key() == QStringLiteral("description")))
      continue;
    if (schema_maps.contains(it.key()) && it.value().isObject()) {
      QJsonObject children;
      const auto map = it.value().toObject();
      for (auto child = map.constBegin(); child != map.constEnd(); ++child)
        children.insert(child.key(), json_schema_without_annotations(child.value(), preserve_descriptions));
      result.insert(it.key(), children);
    } else if (schema_arrays.contains(it.key()) || schema_children.contains(it.key())) {
      result.insert(it.key(), json_schema_without_annotations(it.value(), preserve_descriptions));
    } else {
      // const/enum/examples payloads and extension data are values, not
      // schemas. Preserve their keys verbatim even when they resemble an
      // annotation keyword.
      result.insert(it.key(), it.value());
    }
  }
  return result;
}

QJsonObject operation_specific_composition_schema(const QJsonObject& input_schema) {
  if (input_schema.contains(QStringLiteral("oneOf"))) return input_schema;
  const auto operation_schema = input_schema.value(QStringLiteral("properties")).toObject()
      .value(QStringLiteral("operation")).toObject();
  const auto operations = operation_schema.value(QStringLiteral("enum")).toArray();
  if (operations.isEmpty()) return input_schema;
  const auto conditions = input_schema.value(QStringLiteral("allOf")).toArray();
  QJsonArray variants;
  QJsonArray remaining_conditions;
  for (const auto& operation_value : operations) {
    if (!operation_value.isString()) continue;
    const auto operation = operation_value.toString();
    QJsonObject branch{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), QJsonObject{
            {QStringLiteral("operation"), QJsonObject{
                {QStringLiteral("enum"), QJsonArray{operation}},
            }},
        }},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("operation")}},
    };
    for (const auto& condition_value : conditions) {
      if (!condition_value.isObject()) continue;
      const auto condition = condition_value.toObject();
      const auto predicate = condition.value(QStringLiteral("if")).toObject();
      if (!json_schema_condition_targets_operation(predicate)) {
        if (!remaining_conditions.contains(condition)) remaining_conditions.append(condition);
        continue;
      }
      const auto condition_matches = json_schema_operation_matches(predicate, operation);
      if (condition_matches) {
        branch = merge_json_schema_constraints(
            branch, condition.value(QStringLiteral("then")).toObject());
      } else if (condition.contains(QStringLiteral("else"))) {
        branch = merge_json_schema_constraints(branch, condition.value(QStringLiteral("else")).toObject());
      }
    }
    variants.append(branch);
  }
  if (variants.isEmpty()) return input_schema;
  auto shared_schema = input_schema;
  shared_schema.remove(QStringLiteral("allOf"));
  if (!remaining_conditions.isEmpty())
    shared_schema.insert(QStringLiteral("allOf"), remaining_conditions);
  shared_schema.insert(QStringLiteral("oneOf"), variants);
  return shared_schema;
}

struct CompositionGenerationContext {
  QString asset_id;
  bool authoritative_inspection_present = false;
  bool layer_grounding_context_present = false;
  QSet<QString> grounded_layer_ids;
  bool native_font_inventory_present = false;
  QVariantList native_font_faces;
  QVariantList current_layer_font_pairs;
  QVariantList inspected_font_pairs;
};

CompositionGenerationContext composition_generation_context(
    const AgentLocalModelPlanRequest& request) {
  CompositionGenerationContext context;
  context.asset_id = request.asset.value(QStringLiteral("assetId")).toString().trimmed();
  if (context.asset_id.isEmpty()) return context;

  const auto font_pairs_from_layers = [](const QVariant& layers_value) {
    QVariantList pairs;
    const auto layers = layers_value.toList();
    for (const auto& layer_value : layers) {
      const auto typography = layer_value.toMap().value(QStringLiteral("textTypography")).toMap();
      const auto family = typography.value(QStringLiteral("family")).toString().trimmed();
      const auto face = typography.value(QStringLiteral("face")).toString().trimmed();
      if (family.isEmpty() || face.isEmpty()) continue;
      const QVariantMap pair{{QStringLiteral("family"), family},
          {QStringLiteral("face"), face}};
      if (!pairs.contains(pair)) pairs.append(pair);
    }
    return pairs;
  };

  for (const auto& prior_value : request.prior_tool_results) {
    const auto prior = prior_value.toMap();
    auto tool_name = prior.value(QStringLiteral("toolName"),
        prior.value(QStringLiteral("name"))).toString();
    if (tool_name.isEmpty()) continue;
    const auto arguments = prior.value(QStringLiteral("arguments")).toMap();
    if (arguments.value(QStringLiteral("assetId")).toString().trimmed() != context.asset_id)
      continue;

    auto result = prior.value(QStringLiteral("result")).toMap();
    if (result.isEmpty()) result = prior;
    if (!result.value(QStringLiteral("ok")).toBool()) continue;
    auto content = result.value(QStringLiteral("structuredContent")).toMap();
    if (content.isEmpty()) {
      content = result.value(QStringLiteral("toolResult")).toMap()
          .value(QStringLiteral("structuredContent")).toMap();
    }

    if (tool_name == QStringLiteral("photo.inspect_composition")) {
      const auto layers_value = content.value(QStringLiteral("layers"));
      if (layers_value.metaType().id() == QMetaType::QVariantList) {
        context.authoritative_inspection_present = true;
        context.layer_grounding_context_present = true;
        context.grounded_layer_ids.clear();
        context.current_layer_font_pairs = font_pairs_from_layers(layers_value);
        for (const auto& layer_value : layers_value.toList()) {
          const auto layer_id = layer_value.toMap().value(QStringLiteral("id")).toString().trimmed();
          if (!layer_id.isEmpty()) context.grounded_layer_ids.insert(layer_id);
        }
      }

      // A no-values inspection is the full native inventory read. Targeted
      // family/face probes return a narrower fontFaces list and must not replace
      // an already retained full inventory.
      const auto requested_values = arguments.value(QStringLiteral("values")).toMap();
      const auto font_faces_value = content.value(QStringLiteral("fontFaces"));
      if (requested_values.isEmpty() &&
          font_faces_value.metaType().id() == QMetaType::QVariantList) {
        context.native_font_inventory_present = true;
        context.native_font_faces = font_faces_value.toList();
      } else if (!requested_values.isEmpty()) {
        context.inspected_font_pairs.clear();
        const auto family = content.value(QStringLiteral("fontFamily")).toString().trimmed();
        const auto face = content.value(QStringLiteral("fontFace")).toString().trimmed();
        if (content.value(QStringLiteral("fontFamilyAvailable")).toBool() &&
            content.value(QStringLiteral("fontFaceAvailable")).toBool() &&
            !family.isEmpty() && !face.isEmpty())
          context.inspected_font_pairs.append(QVariantMap{{QStringLiteral("family"), family},
              {QStringLiteral("face"), face}});
      }
      continue;
    }

    if (tool_name != QStringLiteral("photo.edit_composition")) continue;
    const auto font_faces_value = content.value(QStringLiteral("fontFaces"));
    if (font_faces_value.metaType().id() == QMetaType::QVariantList) {
      context.native_font_inventory_present = true;
      context.native_font_faces = font_faces_value.toList();
    }
    const auto layer_values = content.value(QStringLiteral("layers"));
    if (layer_values.metaType().id() == QMetaType::QVariantList)
      context.current_layer_font_pairs = font_pairs_from_layers(layer_values);
    const auto created_layer_id = content.value(QStringLiteral("createdLayerId")).toString().trimmed();
    if (!created_layer_id.isEmpty()) {
      context.layer_grounding_context_present = true;
      context.grounded_layer_ids.insert(created_layer_id);
    }
    const auto created_layer_ids = content.value(QStringLiteral("createdLayerIds")).toList();
    for (const auto& value : created_layer_ids) {
      const auto layer_id = value.toString().trimmed();
      if (!layer_id.isEmpty()) {
        context.layer_grounding_context_present = true;
        context.grounded_layer_ids.insert(layer_id);
      }
    }
  }
  return context;
}

bool composition_operation_requires_layer_id(
    const QJsonObject& input_schema, const QString& operation) {
  const auto expanded = operation_specific_composition_schema(input_schema);
  for (const auto& variant_value : expanded.value(QStringLiteral("oneOf")).toArray()) {
    if (!variant_value.isObject()) continue;
    const auto variant = variant_value.toObject();
    if (json_schema_operation_matches(variant, operation) &&
        variant.value(QStringLiteral("required")).toArray().contains(QStringLiteral("layerId")))
      return true;
  }
  return false;
}

QStringList native_font_faces_for_family(
    const QVariantList& inventory, const QString& family) {
  QStringList faces;
  for (const auto& inventory_value : inventory) {
    const auto entry = inventory_value.toMap();
    if (entry.value(QStringLiteral("family")).toString() != family) continue;
    const auto face_values = entry.value(QStringLiteral("faces")).toList();
    for (const auto& face_value : face_values) {
      const auto face = face_value.toString();
      if (!face.isEmpty() && !faces.contains(face)) faces.append(face);
    }
    break;
  }
  return faces;
}

QVariantList compact_font_face_inventory(
    const QVariantList& inventory,
    qsizetype* omitted_family_count,
    qsizetype* omitted_face_count);

QJsonObject operation_specific_composition_generation_schema(
    const QJsonObject& input_schema, bool require_font_face_pair = false,
    const CompositionGenerationContext* context = nullptr) {
  const auto expanded = operation_specific_composition_schema(input_schema);
  const auto variants = expanded.value(QStringLiteral("oneOf")).toArray();
  if (variants.isEmpty()) return input_schema;

  const auto shared_properties = expanded.value(QStringLiteral("properties")).toObject();
  const auto shared_values = shared_properties.value(QStringLiteral("values")).toObject();
  QJsonArray generation_variants;
  for (const auto& variant_value : variants) {
    if (!variant_value.isObject()) continue;
    const auto variant = variant_value.toObject();
    auto properties = shared_properties;
    auto required = expanded.value(QStringLiteral("required")).toArray();
    const auto variant_properties = variant.value(QStringLiteral("properties")).toObject();
    const auto operation_schema = variant_properties.value(QStringLiteral("operation")).toObject();
    const auto operation_values = operation_schema.value(QStringLiteral("enum")).toArray();
    const auto operation_constant = operation_schema.value(QStringLiteral("const"));
    const auto operation = !operation_constant.isUndefined() && operation_constant.isString()
        ? operation_constant.toString()
        : operation_values.isEmpty() || !operation_values.first().isString()
            ? QString{} : operation_values.first().toString();
    const auto variant_required = variant.value(QStringLiteral("required")).toArray();
    const auto layer_target_operation = variant_required.contains(QStringLiteral("layerId"));
    if (context != nullptr && context->layer_grounding_context_present &&
        layer_target_operation && context->grounded_layer_ids.isEmpty())
      continue;
    if (context != nullptr && context->authoritative_inspection_present &&
        require_font_face_pair && operation == QStringLiteral("typography") &&
        (!context->native_font_inventory_present || context->native_font_faces.isEmpty()))
      continue;
    bool explicitly_closed_values_schema = false;
    if (!operation.isEmpty()) {
      const auto values_schema_is_closed = [](const QJsonObject& values_schema) {
        return values_schema.value(QStringLiteral("type")).toString() == QStringLiteral("object") &&
            values_schema.value(QStringLiteral("additionalProperties")).isBool() &&
            !values_schema.value(QStringLiteral("additionalProperties")).toBool() &&
            values_schema.value(QStringLiteral("properties")).isObject();
      };
      for (const auto& condition_value : input_schema.value(QStringLiteral("allOf")).toArray()) {
        if (!condition_value.isObject()) continue;
        const auto condition = condition_value.toObject();
        const auto predicate = condition.value(QStringLiteral("if")).toObject();
        if (!json_schema_condition_targets_operation(predicate) ||
            !json_schema_operation_matches(predicate, operation))
          continue;
        const auto values_constraint = condition.value(QStringLiteral("then")).toObject()
            .value(QStringLiteral("properties")).toObject()
            .value(QStringLiteral("values")).toObject();
        if (values_schema_is_closed(values_constraint)) {
          explicitly_closed_values_schema = true;
          break;
        }
      }
      if (!explicitly_closed_values_schema) {
        for (const auto& variant_value : input_schema.value(QStringLiteral("oneOf")).toArray()) {
          if (!variant_value.isObject()) continue;
          const auto variant = variant_value.toObject();
          if (!json_schema_operation_matches(variant, operation)) continue;
          const auto values_constraint = variant.value(QStringLiteral("properties")).toObject()
              .value(QStringLiteral("values")).toObject();
          if (values_schema_is_closed(values_constraint)) {
            explicitly_closed_values_schema = true;
            break;
          }
        }
      }
    }

    for (auto it = variant_properties.constBegin(); it != variant_properties.constEnd(); ++it) {
      if (it.key() == QStringLiteral("values")) {
        const auto constraint_values = it.value().toObject();
        const auto constraint_properties =
            constraint_values.value(QStringLiteral("properties")).toObject();
        const auto base_properties = shared_values.value(QStringLiteral("properties")).toObject();
        if (explicitly_closed_values_schema) {
          // Preserve a closed values object only when the original operation
          // branch explicitly declares one. Generic branches inherit the
          // shared closed union schema, which is too narrow for the decoder.
          // Keep every native property and constraint on this operation branch.
          auto strict_values = constraint_values;
          properties.insert(QStringLiteral("values"), strict_values);
        } else {
          auto values = shared_values;
          values.remove(QStringLiteral("properties"));
          values.remove(QStringLiteral("required"));
          // The prompt still carries the full canonical value union. Keeping
          // this decoder branch open avoids dropping optional union controls;
          // the unchanged native input schema remains the execution authority.
          values.insert(QStringLiteral("additionalProperties"), true);
          QJsonObject value_properties;
          const auto required_values = constraint_values.value(QStringLiteral("required")).toArray();
          QJsonArray merged_required_values;
          for (const auto& required_value : required_values) {
            if (!required_value.isString()) continue;
            const auto key = required_value.toString();
            auto value_schema = base_properties.value(key).toObject();
            const auto operation_value_schema = constraint_properties.value(key).toObject();
            if (value_schema.isEmpty() && shared_values.value(QStringLiteral("additionalProperties")).isObject())
              value_schema = shared_values.value(QStringLiteral("additionalProperties")).toObject();
            if (!operation_value_schema.isEmpty())
              value_schema = merge_json_schema_constraints(value_schema, operation_value_schema);
            if (!value_schema.isEmpty()) value_properties.insert(key, value_schema);
            if (!merged_required_values.contains(key)) merged_required_values.append(key);
          }
          // The canonical prompt schema carries all optional value properties.
          // Repeating the entire broad union on every operation branch made
          // the decoder schema tens of kilobytes larger without adding an
          // execution guarantee: native preflight still validates each exact
          // call against the original conditional schema. Keep decoder types
          // for operation-required values, and leave other canonical values
          // open so optional controls remain generatable.
          values.insert(QStringLiteral("properties"), value_properties);
          if (!merged_required_values.isEmpty())
            values.insert(QStringLiteral("required"), merged_required_values);
          for (auto constraint_it = constraint_values.constBegin();
               constraint_it != constraint_values.constEnd(); ++constraint_it) {
            if (constraint_it.key() == QStringLiteral("properties") ||
                constraint_it.key() == QStringLiteral("required") ||
                constraint_it.key() == QStringLiteral("additionalProperties"))
              continue;
            values.insert(constraint_it.key(), constraint_it.value());
          }
          properties.insert(QStringLiteral("values"), values);
        }
      } else {
        auto property_schema = properties.value(it.key()).toObject();
        if (it.value().isObject() && !property_schema.isEmpty())
          property_schema = merge_json_schema_constraints(property_schema, it.value().toObject());
        else
          property_schema = it.value().toObject();
        properties.insert(it.key(), property_schema);
      }
    }
    if (!variant_properties.contains(QStringLiteral("values")) &&
        properties.value(QStringLiteral("values")).isObject()) {
      // An operation with no operation-specific values constraint still
      // inherits the broad shared union from the root properties above.
      // Leaving it closed repeats that entire union on every unconstrained
      // operation branch. Keep its shared object constraints, but make the
      // decoder branch open; the canonical input schema remains the host
      // validation authority for any optional field the model emits.
      auto values = properties.value(QStringLiteral("values")).toObject();
      values.remove(QStringLiteral("properties"));
      values.remove(QStringLiteral("required"));
      values.insert(QStringLiteral("additionalProperties"), true);
      properties.insert(QStringLiteral("values"), values);
    }

    // The compact generic decoder branch drops the canonical values union to
    // avoid repeating dozens of optional controls on every operation. Keep
    // only the two supported native font selectors on typography so the
    // inventory below can add exact enum choices and an explicitly requested
    // family/face pair can be required. The host still validates against the
    // complete canonical schema. Closed operation-specific branches keep
    // their exact authored property set.
    if (operation == QStringLiteral("typography") && !explicitly_closed_values_schema &&
        properties.value(QStringLiteral("values")).isObject()) {
      auto values = properties.value(QStringLiteral("values")).toObject();
      auto values_properties = values.value(QStringLiteral("properties")).toObject();
      const auto canonical_values_properties = shared_values.value(QStringLiteral("properties")).toObject();
      for (const auto& field : {QStringLiteral("family"), QStringLiteral("face")}) {
        if (!values_properties.contains(field) && canonical_values_properties.contains(field))
          values_properties.insert(field, canonical_values_properties.value(field));
      }
      values.insert(QStringLiteral("properties"), values_properties);
      properties.insert(QStringLiteral("values"), values);
    }
    for (const auto& required_value : variant.value(QStringLiteral("required")).toArray())
      if (!required.contains(required_value)) required.append(required_value);

    if (context != nullptr && context->layer_grounding_context_present &&
        layer_target_operation && !context->grounded_layer_ids.isEmpty()) {
      auto layer_id_schema = properties.value(QStringLiteral("layerId")).toObject();
      QJsonArray grounded_ids;
      auto sorted_grounded_ids = context->grounded_layer_ids.values();
      std::sort(sorted_grounded_ids.begin(), sorted_grounded_ids.end());
      for (const auto& layer_id : sorted_grounded_ids)
        grounded_ids.append(layer_id);
      layer_id_schema.insert(QStringLiteral("enum"), grounded_ids);
      properties.insert(QStringLiteral("layerId"), layer_id_schema);
    }

    auto generation_variant = expanded;
    generation_variant.remove(QStringLiteral("oneOf"));
    generation_variant.insert(QStringLiteral("properties"), properties);
    generation_variant.insert(QStringLiteral("required"), required);
    for (auto it = variant.constBegin(); it != variant.constEnd(); ++it) {
      if (it.key() == QStringLiteral("properties") ||
          it.key() == QStringLiteral("required"))
        continue;
      if (it.key() == QStringLiteral("allOf")) {
        auto all_of = generation_variant.value(QStringLiteral("allOf")).toArray();
        for (const auto& constraint : it.value().toArray())
          if (!all_of.contains(constraint)) all_of.append(constraint);
        generation_variant.insert(QStringLiteral("allOf"), all_of);
      } else {
        generation_variant.insert(it.key(), it.value());
      }
    }
    generation_variants.append(generation_variant);
  }
  if (generation_variants.isEmpty()) {
    auto empty_generation_schema = expanded;
    empty_generation_schema.remove(QStringLiteral("properties"));
    empty_generation_schema.remove(QStringLiteral("required"));
    empty_generation_schema.insert(QStringLiteral("oneOf"), QJsonArray{});
    return empty_generation_schema;
  }

  QJsonObject generation_schema;
  for (const auto& key : {QStringLiteral("$schema"), QStringLiteral("$defs"),
           QStringLiteral("definitions"), QStringLiteral("description")})
    if (expanded.contains(key)) generation_schema.insert(key, expanded.value(key));
  generation_schema.insert(QStringLiteral("oneOf"), generation_variants);
  return generation_schema;
}

QJsonObject constrain_composition_font_generation_schema(
    QJsonObject schema, bool require_font_face_pair,
    const CompositionGenerationContext* context) {
  QStringList native_families;
  QStringList native_faces;
  if (context != nullptr && context->native_font_inventory_present) {
    const auto decoder_font_inventory = compact_font_face_inventory(
        context->native_font_faces, nullptr, nullptr);
    for (const auto& entry_value : decoder_font_inventory) {
      const auto entry = entry_value.toMap();
      const auto family = entry.value(QStringLiteral("family")).toString();
      if (family.isEmpty()) continue;
      if (!native_families.contains(family)) native_families.append(family);
      for (const auto& face_value : entry.value(QStringLiteral("faces")).toList()) {
        const auto face = face_value.toString();
        if (!face.isEmpty() && !native_faces.contains(face)) native_faces.append(face);
      }
    }
    const auto append_retained_pair = [&](const QVariantMap& pair) {
      const auto family = pair.value(QStringLiteral("family")).toString().trimmed();
      const auto face = pair.value(QStringLiteral("face")).toString().trimmed();
      if (family.isEmpty() || face.isEmpty() ||
          !native_font_faces_for_family(context->native_font_faces, family).contains(face))
        return;
      if (!native_families.contains(family)) native_families.append(family);
      if (!native_faces.contains(face)) native_faces.append(face);
    };
    for (const auto& pair : context->current_layer_font_pairs)
      append_retained_pair(pair.toMap());
    for (const auto& pair : context->inspected_font_pairs)
      append_retained_pair(pair.toMap());
  }

  auto variants = schema.value(QStringLiteral("oneOf")).toArray();
  for (qsizetype index = 0; index < variants.size(); ++index) {
    if (!variants.at(index).isObject()) continue;
    auto variant = variants.at(index).toObject();
    if (!json_schema_operation_matches(variant, QStringLiteral("typography"))) continue;
    auto properties = variant.value(QStringLiteral("properties")).toObject();
    auto values = properties.value(QStringLiteral("values")).toObject();
    auto value_properties = values.value(QStringLiteral("properties")).toObject();
    const auto add_native_enum = [&value_properties](
        const QString& field, const QStringList& native_values) {
      if (native_values.isEmpty() || !value_properties.contains(field)) return;
      auto property_schema = value_properties.value(field).toObject();
      QJsonArray allowed;
      const auto existing_enum = property_schema.value(QStringLiteral("enum")).toArray();
      for (const auto& native_value : native_values) {
        if (!existing_enum.isEmpty() && !existing_enum.contains(native_value)) continue;
        allowed.append(native_value);
      }
      if (!allowed.isEmpty()) {
        property_schema.insert(QStringLiteral("enum"), allowed);
        value_properties.insert(field, property_schema);
      }
    };
    add_native_enum(QStringLiteral("family"), native_families);
    add_native_enum(QStringLiteral("face"), native_faces);
    values.insert(QStringLiteral("properties"), value_properties);
    if (require_font_face_pair &&
        value_properties.contains(QStringLiteral("family")) &&
        value_properties.contains(QStringLiteral("face"))) {
      auto required_values = values.value(QStringLiteral("required")).toArray();
      for (const auto& field : {QStringLiteral("family"), QStringLiteral("face")})
        if (!required_values.contains(field)) required_values.append(field);
      values.insert(QStringLiteral("required"), required_values);
    }
    properties.insert(QStringLiteral("values"), values);
    variant.insert(QStringLiteral("properties"), properties);
    variants.replace(index, variant);
  }
  if (!variants.isEmpty()) schema.insert(QStringLiteral("oneOf"), variants);
  return schema;
}

QJsonObject restore_composition_font_generation_enums(
    QJsonObject schema, bool require_font_face_pair,
    const CompositionGenerationContext* context) {
  if (context == nullptr || !context->native_font_inventory_present) return schema;
  auto decision_variants = schema.value(QStringLiteral("oneOf")).toArray();
  for (qsizetype decision_index = 0; decision_index < decision_variants.size(); ++decision_index) {
    if (!decision_variants.at(decision_index).isObject()) continue;
    auto decision_variant = decision_variants.at(decision_index).toObject();
    const auto decision_properties = decision_variant.value(QStringLiteral("properties")).toObject();
    if (!decision_properties.value(QStringLiteral("decision")).toObject()
            .value(QStringLiteral("enum")).toArray()
            .contains(QStringLiteral("tool_call_plan")))
      continue;

    auto calls = decision_properties.value(QStringLiteral("toolCalls")).toObject();
    auto item = calls.value(QStringLiteral("items")).toObject();
    auto call_variants = item.value(QStringLiteral("oneOf")).toArray();
    bool changed = false;
    for (qsizetype call_index = 0; call_index < call_variants.size(); ++call_index) {
      if (!call_variants.at(call_index).isObject()) continue;
      auto call_variant = call_variants.at(call_index).toObject();
      auto call_properties = call_variant.value(QStringLiteral("properties")).toObject();
      if (!call_properties.value(QStringLiteral("name")).toObject()
              .value(QStringLiteral("enum")).toArray()
              .contains(QStringLiteral("photo.edit_composition")))
        continue;
      auto arguments = call_properties.value(QStringLiteral("arguments")).toObject();
      arguments = constrain_composition_font_generation_schema(
          std::move(arguments), require_font_face_pair, context);
      call_properties.insert(QStringLiteral("arguments"), arguments);
      call_variant.insert(QStringLiteral("properties"), call_properties);
      call_variants.replace(call_index, call_variant);
      changed = true;
    }
    if (!changed) continue;
    item.insert(QStringLiteral("oneOf"), call_variants);
    calls.insert(QStringLiteral("items"), item);
    auto updated_properties = decision_variant.value(QStringLiteral("properties")).toObject();
    updated_properties.insert(QStringLiteral("toolCalls"), calls);
    decision_variant.insert(QStringLiteral("properties"), updated_properties);
    decision_variants.replace(decision_index, decision_variant);
  }
  if (!decision_variants.isEmpty()) schema.insert(QStringLiteral("oneOf"), decision_variants);
  return schema;
}

QStringList json_schema_validation_errors(
    const QJsonValue& value, const QJsonObject& schema, const QString& path) {
  QStringList errors;
  const auto type_value = schema.value(QStringLiteral("type"));
  const auto type_matches = [&value](const QString& type) {
    if (type == QStringLiteral("object")) return value.isObject();
    if (type == QStringLiteral("array")) return value.isArray();
    if (type == QStringLiteral("string")) return value.isString();
    if (type == QStringLiteral("number")) return value.isDouble();
    if (type == QStringLiteral("integer"))
      return value.isDouble() && std::floor(value.toDouble()) == value.toDouble();
    if (type == QStringLiteral("boolean")) return value.isBool();
    if (type == QStringLiteral("null")) return value.isNull();
    return true;
  };
  if (type_value.isString() && !type_matches(type_value.toString())) {
    errors.append(QStringLiteral("%1 must be %2").arg(path, type_value.toString()));
    return errors;
  }
  if (type_value.isArray()) {
    const auto types = type_value.toArray();
    const auto matches_any = std::any_of(types.cbegin(), types.cend(), [&type_matches](const QJsonValue& item) {
      return item.isString() && type_matches(item.toString());
    });
    if (!matches_any) {
      errors.append(QStringLiteral("%1 has an invalid type").arg(path));
      return errors;
    }
  }
  if (schema.contains(QStringLiteral("const")) && value != schema.value(QStringLiteral("const")))
    errors.append(QStringLiteral("%1 must equal its required constant").arg(path));
  const auto enum_values = schema.value(QStringLiteral("enum")).toArray();
  if (!enum_values.isEmpty() && !enum_values.contains(value))
    errors.append(QStringLiteral("%1 must be one of the allowed values").arg(path));
  if (value.isObject()) {
    const auto object = value.toObject();
    const auto properties = schema.value(QStringLiteral("properties")).toObject();
    for (const auto& required_value : schema.value(QStringLiteral("required")).toArray()) {
      const auto required = required_value.toString();
      if (!object.contains(required)) errors.append(QStringLiteral("%1.%2 is required").arg(path, required));
    }
    if (schema.contains(QStringLiteral("minProperties")) &&
        object.size() < schema.value(QStringLiteral("minProperties")).toInt())
      errors.append(QStringLiteral("%1 must contain at least %2 properties").arg(path).arg(schema.value(QStringLiteral("minProperties")).toInt()));
    if (schema.contains(QStringLiteral("maxProperties")) &&
        object.size() > schema.value(QStringLiteral("maxProperties")).toInt())
      errors.append(QStringLiteral("%1 must contain at most %2 properties").arg(path).arg(schema.value(QStringLiteral("maxProperties")).toInt()));
    const auto additional = schema.value(QStringLiteral("additionalProperties"));
    for (auto it = object.begin(); it != object.end(); ++it) {
      if (properties.contains(it.key())) {
        errors += json_schema_validation_errors(it.value(), properties.value(it.key()).toObject(), path + QLatin1Char('.') + it.key());
      } else if (additional.isBool() && !additional.toBool()) {
        errors.append(QStringLiteral("%1.%2 is not allowed").arg(path, it.key()));
      } else if (additional.isObject()) {
        errors += json_schema_validation_errors(it.value(), additional.toObject(), path + QLatin1Char('.') + it.key());
      }
    }
  } else if (value.isArray()) {
    const auto array = value.toArray();
    if (schema.contains(QStringLiteral("minItems")) && array.size() < schema.value(QStringLiteral("minItems")).toInt())
      errors.append(QStringLiteral("%1 must contain at least %2 items").arg(path).arg(schema.value(QStringLiteral("minItems")).toInt()));
    if (schema.contains(QStringLiteral("maxItems")) && array.size() > schema.value(QStringLiteral("maxItems")).toInt())
      errors.append(QStringLiteral("%1 must contain at most %2 items").arg(path).arg(schema.value(QStringLiteral("maxItems")).toInt()));
    if (schema.value(QStringLiteral("items")).isObject()) {
      for (qsizetype i = 0; i < array.size(); ++i)
        errors += json_schema_validation_errors(array.at(i), schema.value(QStringLiteral("items")).toObject(), path + QStringLiteral("[%1]").arg(i));
    }
  } else if (value.isString()) {
    const auto string = value.toString();
    if (schema.contains(QStringLiteral("minLength")) && string.size() < schema.value(QStringLiteral("minLength")).toInt())
      errors.append(QStringLiteral("%1 must contain at least %2 characters").arg(path).arg(schema.value(QStringLiteral("minLength")).toInt()));
    if (schema.contains(QStringLiteral("maxLength")) && string.size() > schema.value(QStringLiteral("maxLength")).toInt())
      errors.append(QStringLiteral("%1 must contain at most %2 characters").arg(path).arg(schema.value(QStringLiteral("maxLength")).toInt()));
    const auto pattern = schema.value(QStringLiteral("pattern")).toString();
    if (!pattern.isEmpty() && !QRegularExpression(pattern).match(string).hasMatch())
      errors.append(QStringLiteral("%1 does not match its required pattern").arg(path));
  } else if (value.isDouble()) {
    const auto number = value.toDouble();
    if (schema.contains(QStringLiteral("minimum")) && number < schema.value(QStringLiteral("minimum")).toDouble())
      errors.append(QStringLiteral("%1 must be at least %2").arg(path).arg(schema.value(QStringLiteral("minimum")).toDouble()));
    if (schema.contains(QStringLiteral("maximum")) && number > schema.value(QStringLiteral("maximum")).toDouble())
      errors.append(QStringLiteral("%1 must be at most %2").arg(path).arg(schema.value(QStringLiteral("maximum")).toDouble()));
    if (schema.contains(QStringLiteral("exclusiveMinimum")) && number <= schema.value(QStringLiteral("exclusiveMinimum")).toDouble())
      errors.append(QStringLiteral("%1 must be greater than %2").arg(path).arg(schema.value(QStringLiteral("exclusiveMinimum")).toDouble()));
    if (schema.contains(QStringLiteral("exclusiveMaximum")) && number >= schema.value(QStringLiteral("exclusiveMaximum")).toDouble())
      errors.append(QStringLiteral("%1 must be less than %2").arg(path).arg(schema.value(QStringLiteral("exclusiveMaximum")).toDouble()));
  }
  for (const auto& branch : schema.value(QStringLiteral("allOf")).toArray())
    errors += json_schema_validation_errors(value, branch.toObject(), path);
  const auto validate_branches = [&value, &path](const QJsonArray& branches, bool exact_one) {
    QStringList branch_errors;
    auto matches = 0;
    for (const auto& branch : branches) {
      const auto result = json_schema_validation_errors(value, branch.toObject(), path);
      if (result.isEmpty()) ++matches;
      else if (branch_errors.isEmpty()) branch_errors = result;
    }
    QStringList errors;
    if (exact_one && matches != 1) errors.append(QStringLiteral("%1 must match exactly one allowed shape").arg(path));
    else if (!exact_one && matches == 0) errors.append(branch_errors.isEmpty()
        ? QStringLiteral("%1 does not match any allowed shape").arg(path) : branch_errors.join(QStringLiteral("; ")));
    return errors;
  };
  if (schema.contains(QStringLiteral("oneOf")))
    errors += validate_branches(schema.value(QStringLiteral("oneOf")).toArray(), true);
  if (schema.contains(QStringLiteral("anyOf")))
    errors += validate_branches(schema.value(QStringLiteral("anyOf")).toArray(), false);
  if (schema.contains(QStringLiteral("if"))) {
    const auto condition_errors = json_schema_validation_errors(value, schema.value(QStringLiteral("if")).toObject(), path);
    const auto branch_name = condition_errors.isEmpty() ? QStringLiteral("then") : QStringLiteral("else");
    if (schema.contains(branch_name))
      errors += json_schema_validation_errors(value, schema.value(branch_name).toObject(), path);
  }
  return errors;
}

QJsonObject ollama_generation_payload_schema(
    bool allow_tool_call_plan, const QVariantList& tools,
    bool require_tool_call_plan = false,
    bool require_font_face_pair = false,
    const CompositionGenerationContext* composition_context = nullptr) {
  if (allow_tool_call_plan) {
    auto schema = ollama_response_payload_schema(
        allow_tool_call_plan, require_tool_call_plan);
    auto properties = schema.value(QStringLiteral("properties")).toObject();
    auto calls = properties.value(QStringLiteral("toolCalls")).toObject();
    auto item = calls.value(QStringLiteral("items")).toObject();
    QJsonArray variants;
    QJsonArray call_variants;
    auto max_calls = 8;
    for (const auto& value : tools) {
      const auto tool = value.toMap();
      const auto name = tool.value(QStringLiteral("name")).toString();
      auto arguments = QJsonObject::fromVariantMap(tool.value(QStringLiteral("inputSchema")).toMap());
      if (name.isEmpty() || arguments.value(QStringLiteral("type")).toString() != QStringLiteral("object")) continue;
      if (name == QStringLiteral("photo.edit_composition"))
        arguments = operation_specific_composition_generation_schema(
            arguments, require_font_face_pair, composition_context);
      arguments = grammar_compatible_json_schema(arguments);
      if (name == QStringLiteral("photo.edit_composition"))
        arguments = constrain_composition_font_generation_schema(
            std::move(arguments), require_font_face_pair, composition_context);
      const auto argument_variants = arguments.value(QStringLiteral("oneOf")).toArray();
      if (name == QStringLiteral("photo.edit_composition") &&
          composition_context != nullptr && composition_context->authoritative_inspection_present &&
          argument_variants.isEmpty() && arguments.contains(QStringLiteral("oneOf")))
        continue;
      const auto append_call_variant = [&call_variants, &name](const QJsonObject& call_arguments) {
        call_variants.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QJsonObject{
                {QStringLiteral("name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("enum"), QJsonArray{name}}}},
                {QStringLiteral("arguments"), call_arguments}}},
            {QStringLiteral("required"), QJsonArray{QStringLiteral("name"), QStringLiteral("arguments")}}});
      };
      if (name == QStringLiteral("photo.edit_composition") && !argument_variants.isEmpty()) {
        for (const auto& argument_variant : argument_variants)
          if (argument_variant.isObject()) append_call_variant(argument_variant.toObject());
      } else {
        append_call_variant(arguments);
      }
      max_calls = std::min(max_calls, std::clamp(tool.value(QStringLiteral("planningMaxCalls"), 8).toInt(), 1, 8));
    }
    if (!call_variants.isEmpty()) {
      item = QJsonObject{{QStringLiteral("oneOf"), call_variants}};
      calls.insert(QStringLiteral("items"), item);
      calls.insert(QStringLiteral("maxItems"), max_calls);
      properties.insert(QStringLiteral("toolCalls"), calls);
      schema.insert(QStringLiteral("properties"), properties);
      variants = call_variants;
    }
    add_ollama_decision_variants(
        &schema, !variants.isEmpty(), require_tool_call_plan);
    schema.remove(QStringLiteral("type"));
    schema.remove(QStringLiteral("properties"));
    schema.remove(QStringLiteral("required"));
    schema = grammar_compatible_json_schema(schema);
    schema = restore_composition_font_generation_enums(
        std::move(schema), require_font_face_pair, composition_context);
    return json_schema_without_annotations(schema).toObject();
  }
  QJsonObject schema{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("properties"),
       QJsonObject{
           {QStringLiteral("decision"),
            QJsonObject{
                {QStringLiteral("type"), QStringLiteral("string")},
                {QStringLiteral("enum"),
                 QJsonArray{
                     QStringLiteral("edit_intent"),
                     QStringLiteral("needs_user_input"),
                     QStringLiteral("cannot_complete_safely"),
                 }},
           }},
           {QStringLiteral("summary"),
            QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
           {QStringLiteral("styleConstraintCitations"),
            QJsonObject{
                {QStringLiteral("type"), QStringLiteral("array")},
                {QStringLiteral("maxItems"), 4},
                {QStringLiteral("items"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
            }},
           {QStringLiteral("editIntent"),
            QJsonObject{
                {QStringLiteral("type"), QStringLiteral("object")},
                {QStringLiteral("properties"),
                 QJsonObject{
                     {QStringLiteral("goalSummary"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                     {QStringLiteral("colorMode"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                     {QStringLiteral("contrast"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                     {QStringLiteral("brightness"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                     {QStringLiteral("palette"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                     {QStringLiteral("saturation"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                     {QStringLiteral("crop"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                     {QStringLiteral("detail"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                     {QStringLiteral("mood"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                     {QStringLiteral("confidence"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
                     {QStringLiteral("globalChangePolicy"),
                      QJsonObject{
                          {QStringLiteral("type"), QStringLiteral("string")},
                          {QStringLiteral("enum"), QJsonArray{QStringLiteral("allow"), QStringLiteral("preserve")}},
                      }},
                     {QStringLiteral("visualFindings"),
                      QJsonObject{
                          {QStringLiteral("type"), QStringLiteral("array")},
                          {QStringLiteral("maxItems"), 4},
                          {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
                      }},
                     {QStringLiteral("localizedActions"),
                      QJsonObject{
                          {QStringLiteral("type"), QStringLiteral("array")},
                          {QStringLiteral("maxItems"), 2},
                          {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
                      }},
                     {QStringLiteral("retouchActions"),
                      QJsonObject{
                          {QStringLiteral("type"), QStringLiteral("array")},
                          {QStringLiteral("maxItems"), 2},
                          {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
                      }},
                 }},
                {QStringLiteral("required"),
                 QJsonArray{
                     QStringLiteral("goalSummary"),
                     QStringLiteral("confidence"),
                     QStringLiteral("globalChangePolicy"),
                     QStringLiteral("visualFindings"),
                     QStringLiteral("localizedActions"),
                     QStringLiteral("retouchActions"),
                 }},
            }},
           {QStringLiteral("reason"),
            QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
           {QStringLiteral("userQuestions"),
            QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
           {QStringLiteral("warnings"),
            QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
       }},
      {QStringLiteral("required"),
       QJsonArray{
           QStringLiteral("decision"),
           QStringLiteral("summary"),
           QStringLiteral("styleConstraintCitations"),
           QStringLiteral("editIntent"),
       }},
  };
  add_ollama_decision_variants(&schema, false);
  const auto schema_properties = schema.value(QStringLiteral("properties")).toObject();
  schema.insert(QStringLiteral("properties"), QJsonObject{
      {QStringLiteral("decision"), schema_properties.value(QStringLiteral("decision"))},
      {QStringLiteral("summary"), schema_properties.value(QStringLiteral("summary"))},
  });
  schema.remove(QStringLiteral("type"));
  schema.remove(QStringLiteral("properties"));
  schema.remove(QStringLiteral("required"));
  return json_schema_without_annotations(grammar_compatible_json_schema(schema)).toObject();
}

bool has_supported_tool_call_schema(const QVariantList& tools) {
  return std::any_of(tools.cbegin(), tools.cend(), [](const QVariant& value) {
    const auto tool = value.toMap();
    return !tool.value(QStringLiteral("name")).toString().trimmed().isEmpty() &&
        tool.value(QStringLiteral("inputSchema")).toMap()
                .value(QStringLiteral("type")).toString() ==
            QStringLiteral("object");
  });
}

QJsonObject ollama_proof_critique_payload_schema() {
  const QJsonObject string_array{
      {QStringLiteral("type"), QStringLiteral("array")},
      {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}};
  return QJsonObject{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("properties"),
       QJsonObject{
           {QStringLiteral("pass"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
           {QStringLiteral("summary"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
           {QStringLiteral("issues"), string_array},
           {QStringLiteral("repairHints"), string_array},
           {QStringLiteral("needsUserReview"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
           {QStringLiteral("tasteConfidence"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
           {QStringLiteral("warnings"), string_array},
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
  const auto project_taste =
      memory_packet.value(QStringLiteral("approvedProjectTasteCard")).toMap();
  if (project_taste.value(QStringLiteral("available")).toBool()) {
    for (const auto& correction_value :
         project_taste.value(QStringLiteral("corrections")).toList()) {
      if (examples.size() >= 2) {
        break;
      }
      const auto correction = correction_value.toMap();
      examples.append(QVariantMap{
          {QStringLiteral("kind"), QStringLiteral("approved-project-correction")},
          {QStringLiteral("projectKey"),
           project_taste.value(QStringLiteral("projectKey"))},
          {QStringLiteral("feedback"),
           correction.value(QStringLiteral("feedback"))},
          {QStringLiteral("rejectedRecipeExamples"),
           correction.value(QStringLiteral("rejectedRecipeExamples"))},
          {QStringLiteral("approvedRecipeExamples"),
           correction.value(QStringLiteral("approvedRecipeExamples"))},
          {QStringLiteral("approvedRunId"),
           correction.value(QStringLiteral("approvedRunId"))},
      });
    }
    for (const auto& exemplar_value :
         project_taste.value(QStringLiteral("exemplars")).toList()) {
      if (examples.size() >= 2) {
        break;
      }
      const auto exemplar = exemplar_value.toMap();
      examples.append(QVariantMap{
          {QStringLiteral("kind"), QStringLiteral("approved-project-edit")},
          {QStringLiteral("projectKey"), project_taste.value(QStringLiteral("projectKey"))},
          {QStringLiteral("runId"), exemplar.value(QStringLiteral("runId"))},
          {QStringLiteral("userIntent"), exemplar.value(QStringLiteral("userIntent"))},
          {QStringLiteral("editIntent"), exemplar.value(QStringLiteral("editIntent"))},
          {QStringLiteral("assetExamples"), exemplar.value(QStringLiteral("assetExamples"))},
          {QStringLiteral("twoSpeedDraftChoice"),
           exemplar.value(QStringLiteral("twoSpeedDraftChoice"))},
      });
    }
    for (const auto& rejected_value :
         project_taste.value(QStringLiteral("rejectedDrafts")).toList()) {
      if (examples.size() >= 3) {
        break;
      }
      const auto rejected = rejected_value.toMap();
      examples.append(QVariantMap{
          {QStringLiteral("kind"), QStringLiteral("rejected-project-edit")},
          {QStringLiteral("projectKey"),
           project_taste.value(QStringLiteral("projectKey"))},
          {QStringLiteral("runId"), rejected.value(QStringLiteral("runId"))},
          {QStringLiteral("recipeExamples"),
           rejected.value(QStringLiteral("recipeExamples"))},
          {QStringLiteral("avoidExactRecipe"), true},
          {QStringLiteral("inferredReason"), false},
      });
    }
  }
  const auto approved_sample =
      memory_packet.value(QStringLiteral("approvedSampleRecipeCard")).toMap();
  if ((phase == QStringLiteral("batch") || phase == QStringLiteral("outlier")) &&
      approved_sample.value(QStringLiteral("available")).toBool() &&
      approved_sample.value(QStringLiteral("validationOk")).toBool() &&
      examples.size() < 4) {
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
  while (examples.size() > 4) {
    examples.removeLast();
  }
  return examples;
}

QVariant compact_prompt_evidence(const QVariant& value, const QString& key = {}, int depth = 0) {
  if (depth > 16) return QStringLiteral("[nested evidence omitted]");
  if (key == QStringLiteral("approvedInstructions") || key == QStringLiteral("exactCopy") ||
      key == QStringLiteral("deliverables") || key == QStringLiteral("targetIds") ||
      key == QStringLiteral("taskScopedCapabilities"))
    return value;
  if (value.typeId() == QMetaType::QByteArray || key.endsWith(QStringLiteral("Data"))) {
    return QVariantMap{{"storedLocally", true}, {"payloadOmitted", true}};
  }
  if (key == QStringLiteral("promptAudit") || key == QStringLiteral("modelPromptAudit"))
    return QStringLiteral("[prior prompt audit omitted]");
  if (value.typeId() == QMetaType::QVariantMap) {
    QVariantMap result;
    const auto map = value.toMap();
    if (map.value(QStringLiteral("kind")).toString() ==
            QStringLiteral("current-composition-assessment") &&
        map.value(QStringLiteral("contract")).toString() ==
            QStringLiteral("agent-current-composition-assessment-v1"))
      return value;
    for (auto it = map.cbegin(); it != map.cend(); ++it) result.insert(it.key(), compact_prompt_evidence(it.value(), it.key(), depth + 1));
    return result;
  }
  if (value.typeId() == QMetaType::QVariantList || value.typeId() == QMetaType::QStringList) {
    QVariantList result;
    const auto list = value.toList();
    for (qsizetype i = 0; i < std::min<qsizetype>(list.size(), 64); ++i) result.append(compact_prompt_evidence(list[i], {}, depth + 1));
    if (list.size() > 64) result.append(QStringLiteral("[additional entries omitted]") );
    return result;
  }
  if (value.typeId() == QMetaType::QString) {
    const auto text = value.toString();
    if (text.startsWith(QLatin1Char('{')) || text.startsWith(QLatin1Char('['))) {
      const auto json = QJsonDocument::fromJson(text.toUtf8());
      if (!json.isNull()) return compact_prompt_evidence(json.toVariant(), {}, depth + 1);
    }
    if (text.size() > 4096) return QStringLiteral("[overlong text omitted: %1 characters]").arg(text.size());
  }
  return value;
}

QString prompt_json(const QVariant& value) {
  const auto json = QJsonDocument::fromVariant(compact_prompt_evidence(value)).toJson(QJsonDocument::Compact);
  if (json.size() > 48000) return QStringLiteral("{\"evidenceOmittedForSize\":true,\"requiresHumanReview\":true}");
  return QString::fromUtf8(json);
}

QVariantMap compact_prompt_columnar_rows(
    const QVariantList& rows,
    const QString& preferred_identity_field) {
  if (rows.isEmpty()) return {};

  QSet<QString> field_set;
  bool has_stable_identity = !preferred_identity_field.isEmpty();
  QSet<QString> identities;
  QVariantList row_ids;
  for (const auto& row_value : rows) {
    const auto row = row_value.toMap();
    for (auto iterator = row.cbegin(); iterator != row.cend(); ++iterator)
      field_set.insert(iterator.key());
    const auto identity = row.value(preferred_identity_field);
    const auto identity_text = identity.toString().trimmed();
    if (!row.contains(preferred_identity_field) ||
        identity.metaType().id() != QMetaType::QString || identity_text.isEmpty() ||
        identities.contains(identity_text)) {
      has_stable_identity = false;
    } else {
      identities.insert(identity_text);
      row_ids.append(identity);
    }
  }

  QStringList fields = field_set.values();
  fields.sort(Qt::CaseSensitive);
  if (has_stable_identity) fields.removeAll(preferred_identity_field);

  QVariantMap common_fields;
  const auto serialized_prompt_value = [](const QVariant& value) {
    const auto compact_value = compact_prompt_evidence(value);
    const auto compact_serialized = QJsonDocument(QJsonArray{
        QJsonValue::fromVariant(compact_value)}).toJson(QJsonDocument::Compact);
    for (const auto& marker : {QByteArray("evidenceOmittedForSize"),
             QByteArray("[overlong text omitted:"),
             QByteArray("[nested evidence omitted]"),
             QByteArray("[additional entries omitted]")}) {
      if (compact_serialized.contains(marker)) return QByteArray{};
    }
    return QJsonDocument(QJsonArray{
        QJsonValue::fromVariant(value)}).toJson(QJsonDocument::Compact);
  };
  for (const auto& field : fields) {
    bool present_in_every_row = true;
    QByteArray common_serialized_value;
    QVariant common_value;
    for (qsizetype row_index = 0; row_index < rows.size(); ++row_index) {
      const auto row = rows.at(row_index).toMap();
      if (!row.contains(field)) {
        present_in_every_row = false;
        break;
      }
      const auto serialized = serialized_prompt_value(row.value(field));
      if (serialized.isEmpty()) {
        present_in_every_row = false;
        break;
      }
      if (row_index == 0) {
        common_serialized_value = serialized;
        common_value = row.value(field);
      } else if (serialized != common_serialized_value) {
        present_in_every_row = false;
        break;
      }
    }
    if (present_in_every_row) {
      common_fields.insert(field, common_value);
    }
  }
  for (auto iterator = common_fields.cbegin(); iterator != common_fields.cend(); ++iterator)
    fields.removeAll(iterator.key());

  QVariantList encoded_rows;
  QVariantList presence_masks;
  encoded_rows.reserve(rows.size());
  presence_masks.reserve(rows.size());
  QString common_presence_mask;
  bool has_common_presence_mask = true;
  for (const auto& row_value : rows) {
    const auto row = row_value.toMap();
    QVariantList values;
    QString mask;
    values.reserve(fields.size());
    mask.reserve(fields.size());
    for (const auto& field : fields) {
      const auto present = row.contains(field);
      mask.append(present ? QLatin1Char('1') : QLatin1Char('0'));
      values.append(present ? row.value(field) : QVariant{});
    }
    if (presence_masks.isEmpty()) common_presence_mask = mask;
    else if (mask != common_presence_mask) has_common_presence_mask = false;
    presence_masks.append(mask);
    // QVariantList's QList overload concatenates another list; wrap each row
    // as a QVariant to preserve the row boundary, including empty rows.
    encoded_rows.append(QVariant{values});
  }

  QVariantMap result{
      {QStringLiteral("encoding"), QStringLiteral("lossless-columnar-rows-v1")},
      {QStringLiteral("fields"), fields},
      {QStringLiteral("rows"), encoded_rows},
  };
  if (has_stable_identity) {
    result.insert(QStringLiteral("identityField"), preferred_identity_field);
    result.insert(QStringLiteral("rowIds"), row_ids);
  }
  if (!common_fields.isEmpty()) result.insert(QStringLiteral("commonFields"), common_fields);
  if (has_common_presence_mask)
    result.insert(QStringLiteral("presenceMask"), common_presence_mask);
  else
    result.insert(QStringLiteral("presenceMasks"), presence_masks);
  return result;
}

QVariantMap compact_prompt_expected_graphic_rows(const QVariantList& rows) {
  if (rows.isEmpty()) return {};
  QVariantList compact_rows;
  QVariantList typography_rows;
  QVariantList layout_rows;
  QSet<QString> identities;
  bool tables_are_bindable = true;
  for (const auto& row_value : rows) {
    auto row = row_value.toMap();
    const auto id_value = row.value(QStringLiteral("id"));
    const auto id = id_value.toString().trimmed();
    const auto typography_value = row.value(QStringLiteral("textTypography"));
    const auto layout_value = row.value(QStringLiteral("layoutMetrics"));
    if (id.isEmpty() || id_value.metaType().id() != QMetaType::QString ||
        identities.contains(id) || typography_value.metaType().id() != QMetaType::QVariantMap ||
        layout_value.metaType().id() != QMetaType::QVariantMap) {
      tables_are_bindable = false;
      break;
    }
    identities.insert(id);
    auto typography = typography_value.toMap();
    auto layout = layout_value.toMap();
    typography.insert(QStringLiteral("id"), id_value);
    layout.insert(QStringLiteral("id"), id_value);
    typography_rows.append(typography);
    layout_rows.append(layout);
    row.remove(QStringLiteral("textTypography"));
    row.remove(QStringLiteral("layoutMetrics"));
    compact_rows.append(row);
  }
  if (!tables_are_bindable)
    return compact_prompt_columnar_rows(rows, QStringLiteral("id"));

  auto result = compact_prompt_columnar_rows(compact_rows, QStringLiteral("id"));
  result.insert(QStringLiteral("textTypographyByLayerId"),
      compact_prompt_columnar_rows(typography_rows, QStringLiteral("id")));
  result.insert(QStringLiteral("layoutMetricsByLayerId"),
      compact_prompt_columnar_rows(layout_rows, QStringLiteral("id")));
  return result;
}

bool has_verified_unchanged_photo_baseline(
    const QVariantMap& validation_report,
    const QVariantMap& asset);

QVariantMap proof_critique_validation_prompt_projection(
    const QVariantMap& validation_report,
    const QVariantList& graphic_layers,
    const QString& user_instructions,
    const QVariantMap& asset) {
  const auto omit_unchanged_photo_distribution_bins = !graphic_layers.isEmpty() &&
      has_verified_unchanged_photo_baseline(validation_report, asset);
  auto projected = validation_report;
  constexpr auto sha256_reference_marker = "$h1";
  constexpr auto sha256_value_table_key = "hashes";
  QHash<QString, int> sha256_occurrences;
  bool sha256_wire_marker_collision = projected.contains(QString::fromLatin1(sha256_value_table_key));
  const auto scan_sha256_values = [&](auto&& self, const QVariant& value,
                                     const QString& field) -> void {
    if (value.metaType().id() == QMetaType::QVariantMap) {
      const auto map = value.toMap();
      if (map.contains(QString::fromLatin1(sha256_reference_marker)))
        sha256_wire_marker_collision = true;
      for (auto iterator = map.cbegin(); iterator != map.cend(); ++iterator)
        self(self, iterator.value(), iterator.key());
    } else if (value.metaType().id() == QMetaType::QVariantList) {
      for (const auto& entry : value.toList()) self(self, entry, field);
    } else if (field.endsWith(QStringLiteral("Sha256")) &&
        value.metaType().id() == QMetaType::QString) {
      const auto hash = value.toString();
      static const QRegularExpression hash_pattern(QStringLiteral("^[0-9a-f]{64}$"));
      if (hash_pattern.match(hash).hasMatch())
        sha256_occurrences.insert(hash, sha256_occurrences.value(hash) + 1);
    }
  };
  scan_sha256_values(scan_sha256_values, projected, {});
  if (!sha256_wire_marker_collision) {
    QStringList repeated_sha256_values;
    for (auto iterator = sha256_occurrences.cbegin(); iterator != sha256_occurrences.cend(); ++iterator)
      if (iterator.value() >= 2) repeated_sha256_values.append(iterator.key());
    repeated_sha256_values.sort(Qt::CaseSensitive);
    if (!repeated_sha256_values.isEmpty()) {
      QHash<QString, int> sha256_indexes;
      QVariantList sha256_values;
      for (const auto& hash : repeated_sha256_values) {
        sha256_indexes.insert(hash, static_cast<int>(sha256_values.size()));
        sha256_values.append(hash);
      }
      const auto replace_sha256_values = [&](auto&& self, QVariant& value,
                                            const QString& field) -> void {
        if (value.metaType().id() == QMetaType::QVariantMap) {
          auto map = value.toMap();
          for (auto iterator = map.begin(); iterator != map.end(); ++iterator)
            self(self, iterator.value(), iterator.key());
          value = map;
        } else if (value.metaType().id() == QMetaType::QVariantList) {
          auto list = value.toList();
          for (auto& entry : list) self(self, entry, field);
          value = list;
        } else if (field.endsWith(QStringLiteral("Sha256")) &&
            value.metaType().id() == QMetaType::QString) {
          const auto index = sha256_indexes.constFind(value.toString());
          if (index != sha256_indexes.cend())
            value = QVariantMap{{QString::fromLatin1(sha256_reference_marker), *index}};
        }
      };
      QVariant compacted = projected;
      replace_sha256_values(replace_sha256_values, compacted, {});
      projected = compacted.toMap();
      projected.insert(QString::fromLatin1(sha256_value_table_key), sha256_values);
    }
  }
  auto composition_review = projected.value(QStringLiteral("compositionReview")).toMap();
  if (!composition_review.isEmpty()) {
    const auto safe_serialized = [](const QVariant& value) {
      const auto compact_value = compact_prompt_evidence(value);
      const auto compact_json = QJsonDocument(QJsonArray{
          QJsonValue::fromVariant(compact_value)}).toJson(QJsonDocument::Compact);
      for (const auto& marker : {QStringLiteral("evidenceOmittedForSize"),
               QStringLiteral("[overlong text omitted:"),
               QStringLiteral("[nested evidence omitted]"),
               QStringLiteral("[additional entries omitted]")}) {
        if (compact_json.contains(marker.toUtf8())) return QString{};
      }
      return QString::fromUtf8(QJsonDocument(QJsonArray{
          QJsonValue::fromVariant(value)}).toJson(QJsonDocument::Compact));
    };

    const auto expected_graphic_layers_json = safe_serialized(graphic_layers);
    const auto observed_layers = composition_review.value(QStringLiteral("observedGraphicLayers"));
    if (!expected_graphic_layers_json.isEmpty() &&
        safe_serialized(observed_layers) == expected_graphic_layers_json) {
      composition_review.remove(QStringLiteral("observedGraphicLayers"));
      composition_review.insert(QStringLiteral("observedGraphicLayersReference"), QVariantMap{
          {QStringLiteral("path"), QStringLiteral("Expected graphic text layers JSON")},
          {QStringLiteral("exactSerializedMatch"), true},
      });
    }

    const auto brief_marker = QStringLiteral("Approved brief:");
    const auto brief_marker_index = user_instructions.indexOf(brief_marker);
    if (brief_marker_index >= 0) {
      const auto brief_json = json_object_text(
          user_instructions.mid(brief_marker_index + brief_marker.size()));
      QJsonParseError parse_error;
      const auto brief_document = QJsonDocument::fromJson(brief_json.toUtf8(), &parse_error);
      if (!brief_json.isEmpty() && parse_error.error == QJsonParseError::NoError &&
          brief_document.isObject()) {
        const auto brief = brief_document.object();
        const auto approved_plan = brief.value(QStringLiteral("plan")).toObject().toVariantMap();
        auto expected_layout = composition_review.value(QStringLiteral("expectedLayoutAndStack")).toMap();
        QVariantList exact_plan_fields;
        for (const auto& key : {QStringLiteral("actions"), QStringLiteral("constraints"),
                 QStringLiteral("copySuggestionApprovalRequired"), QStringLiteral("deliverables"),
                 QStringLiteral("exactCopy"), QStringLiteral("outcome"), QStringLiteral("summary")}) {
          if (!expected_layout.contains(key) || !approved_plan.contains(key)) continue;
          const auto expected_json = safe_serialized(expected_layout.value(key));
          const auto approved_json = safe_serialized(approved_plan.value(key));
          if (!expected_json.isEmpty() && expected_json == approved_json) {
            expected_layout.remove(key);
            exact_plan_fields.append(key);
          }
        }
        if (!exact_plan_fields.isEmpty()) {
          expected_layout.insert(QStringLiteral("approvedBriefPlanReference"),
              QStringLiteral("Approved brief.plan"));
          expected_layout.insert(QStringLiteral("approvedBriefPlanExactFields"), exact_plan_fields);
          composition_review.insert(QStringLiteral("expectedLayoutAndStack"), expected_layout);
        }

        const auto expected_deliverables = composition_review.value(QStringLiteral("expectedDeliverables"));
        const auto approved_deliverables = brief.value(QStringLiteral("deliverables")).toVariant();
        const auto expected_deliverables_json = safe_serialized(expected_deliverables);
        const auto approved_deliverables_json = safe_serialized(approved_deliverables);
        if (!expected_deliverables_json.isEmpty() &&
            expected_deliverables_json == approved_deliverables_json) {
          composition_review.remove(QStringLiteral("expectedDeliverables"));
          composition_review.insert(QStringLiteral("expectedDeliverablesReference"),
              QVariantMap{
                  {QStringLiteral("path"), QStringLiteral("Approved brief.deliverables")},
                  {QStringLiteral("exactSerializedMatch"), true},
              });
        }
      }
    }
  }

  auto inspection = composition_review.value(QStringLiteral("inspection")).toMap();
  auto structured_content = inspection.value(QStringLiteral("structuredContent")).toMap();
  auto inspection_layers = structured_content.value(QStringLiteral("layers")).toList();
  if (!inspection_layers.isEmpty()) {
    QHash<QString, QVariantMap> expected_layers_by_id;
    QHash<QString, int> expected_layer_id_counts;
    for (const auto& layer_value : graphic_layers) {
      const auto layer = layer_value.toMap();
      const auto layer_id = layer.value(QStringLiteral("id")).toString().trimmed();
      if (!layer_id.isEmpty()) {
        expected_layers_by_id.insert(layer_id, layer);
        expected_layer_id_counts.insert(layer_id, expected_layer_id_counts.value(layer_id) + 1);
      }
    }
    for (auto& layer_value : inspection_layers) {
      auto layer = layer_value.toMap();
      const auto layer_id = layer.value(QStringLiteral("id")).toString().trimmed();
      if (expected_layer_id_counts.value(layer_id) != 1) continue;
      const auto expected = expected_layers_by_id.value(layer_id);
      if (expected.isEmpty()) continue;
      QStringList exact_fields;
      for (auto iterator = layer.cbegin(); iterator != layer.cend(); ++iterator) {
        if (iterator.key() == QStringLiteral("id") || !expected.contains(iterator.key())) continue;
        const auto comparable_json = [](const QVariant& value) {
          const auto compact_value = compact_prompt_evidence(value);
          const auto compact_serialized = QJsonDocument(QJsonArray{
              QJsonValue::fromVariant(compact_value)}).toJson(QJsonDocument::Compact);
          for (const auto& marker : {QByteArray("evidenceOmittedForSize"),
                   QByteArray("[overlong text omitted:"),
                   QByteArray("[nested evidence omitted]"),
                   QByteArray("[additional entries omitted]")}) {
            if (compact_serialized.contains(marker)) return QByteArray{};
          }
          return QJsonDocument(QJsonArray{
              QJsonValue::fromVariant(value)}).toJson(QJsonDocument::Compact);
        };
        const auto left = comparable_json(iterator.value());
        const auto right = comparable_json(expected.value(iterator.key()));
        if (!left.isEmpty() && left == right)
          exact_fields.append(iterator.key());
      }
      for (const auto& field : exact_fields) layer.remove(field);
      if (!exact_fields.isEmpty()) {
        layer.insert(QStringLiteral("expectedGraphicTextLayerReference"),
            QStringLiteral("Expected graphic text layers JSON row id=%1").arg(layer_id));
        layer.insert(QStringLiteral("exactFieldMatches"), exact_fields);
      }
      layer_value = layer;
    }
    structured_content.insert(QStringLiteral("layers"),
        compact_prompt_columnar_rows(inspection_layers, QStringLiteral("id")));
    inspection.insert(QStringLiteral("structuredContent"), structured_content);
    composition_review.insert(QStringLiteral("inspection"), inspection);
  }

  auto geometry_checks = composition_review.value(QStringLiteral("geometryChecks")).toList();
  if (!geometry_checks.isEmpty()) {
    QHash<QString, QVariantMap> expected_graphics_by_id;
    QHash<QString, int> expected_graphic_id_counts;
    for (const auto& layer_value : graphic_layers) {
      const auto layer = layer_value.toMap();
      const auto id = layer.value(QStringLiteral("id")).toString().trimmed();
      if (id.isEmpty()) continue;
      expected_graphics_by_id.insert(id, layer);
      expected_graphic_id_counts.insert(id, expected_graphic_id_counts.value(id) + 1);
    }
    const auto comparable_json = [](const QVariant& value) {
      const auto compact_value = compact_prompt_evidence(value);
      const auto compact_serialized = QJsonDocument(QJsonArray{
          QJsonValue::fromVariant(compact_value)}).toJson(QJsonDocument::Compact);
      for (const auto& marker : {QByteArray("evidenceOmittedForSize"),
               QByteArray("[overlong text omitted:"),
               QByteArray("[nested evidence omitted]"),
               QByteArray("[additional entries omitted]")}) {
        if (compact_serialized.contains(marker)) return QByteArray{};
      }
      return QJsonDocument(QJsonArray{
          QJsonValue::fromVariant(value)}).toJson(QJsonDocument::Compact);
    };
    QVariantList compact_geometry_checks;
    QStringList common_layout_metric_fields;
    bool geometry_references_are_unambiguous = true;
    for (const auto& geometry_value : geometry_checks) {
      auto geometry = geometry_value.toMap();
      const auto id = geometry.value(QStringLiteral("layerId")).toString().trimmed();
      if (id.isEmpty() || expected_graphic_id_counts.value(id) != 1) {
        geometry_references_are_unambiguous = false;
        break;
      }
      const auto layout_metrics = expected_graphics_by_id.value(id)
          .value(QStringLiteral("layoutMetrics")).toMap();
      QStringList exact_fields;
      for (auto iterator = geometry.cbegin(); iterator != geometry.cend(); ++iterator) {
        if (!layout_metrics.contains(iterator.key())) continue;
        const auto observed = comparable_json(iterator.value());
        const auto expected = comparable_json(layout_metrics.value(iterator.key()));
        if (!observed.isEmpty() && observed == expected) exact_fields.append(iterator.key());
      }
      if (exact_fields.isEmpty() ||
          (!common_layout_metric_fields.isEmpty() && exact_fields != common_layout_metric_fields)) {
        geometry_references_are_unambiguous = false;
        break;
      }
      common_layout_metric_fields = exact_fields;
      for (const auto& field : exact_fields) geometry.remove(field);
      geometry.insert(QStringLiteral("layoutMetricsReference"), true);
      compact_geometry_checks.append(geometry);
    }
    if (geometry_references_are_unambiguous && !common_layout_metric_fields.isEmpty()) {
      composition_review.insert(QStringLiteral("geometryChecksLayoutMetricsReferenceFields"),
          common_layout_metric_fields);
      composition_review.insert(QStringLiteral("geometryChecks"),
          compact_prompt_columnar_rows(compact_geometry_checks, QStringLiteral("layerId")));
    }
  }

  auto detail_report = composition_review.value(QStringLiteral("detailProofReport")).toMap();
  auto detail_rows = detail_report.value(QStringLiteral("targetedDetailProofs")).toList();
  const auto requested_overview_path = asset.value(QStringLiteral("proofImagePath")).toString();
  const auto requested_detail_paths = asset.value(QStringLiteral("additionalProofImagePaths")).toStringList();
  const auto asset_evidence = asset.value(QStringLiteral("visualEvidence")).toMap();
  const auto asset_detail_metadata = asset_evidence.value(QStringLiteral("targetedDetailProofs")).toList();
  const auto report_paths = detail_report.value(QStringLiteral("detailProofImagePaths")).toStringList();
  const auto paths_are_attached_images = !requested_overview_path.isEmpty() &&
      detail_report.value(QStringLiteral("overviewPath")).toString() == requested_overview_path &&
      !requested_detail_paths.isEmpty() && requested_detail_paths.size() == detail_rows.size() &&
      requested_detail_paths == report_paths && asset_detail_metadata.size() == detail_rows.size();
  if (paths_are_attached_images) {
    bool every_path_matches = true;
    for (qsizetype index = 0; index < detail_rows.size(); ++index) {
      const auto path = requested_detail_paths.at(index);
      if (asset_detail_metadata.at(index).toMap().value(QStringLiteral("imagePath")).toString() != path ||
          detail_rows.at(index).toMap().value(QStringLiteral("imagePath")).toString() != path) {
        every_path_matches = false;
        break;
      }
    }
    if (every_path_matches) {
      detail_report.remove(QStringLiteral("overviewPath"));
      detail_report.insert(QStringLiteral("overviewImageIndex"), 0);
      detail_report.remove(QStringLiteral("detailProofImagePaths"));
      QVariantList detail_image_indexes;
      for (qsizetype index = 0; index < requested_detail_paths.size(); ++index) {
        detail_image_indexes.append(index + 1);
        auto row = detail_rows.at(index).toMap();
        row.remove(QStringLiteral("imagePath"));
        row.insert(QStringLiteral("attachedImageIndex"), index + 1);
        detail_rows[index] = row;
      }
      detail_report.insert(QStringLiteral("detailProofImageIndices"), detail_image_indexes);
      detail_report.insert(QStringLiteral("targetedDetailProofs"),
          compact_prompt_columnar_rows(detail_rows, QStringLiteral("layerId")));
      composition_review.remove(QStringLiteral("detailProofImagePathsReference"));
      composition_review.insert(QStringLiteral("detailProofImageIndicesReference"),
          QStringLiteral("validationReport.compositionReview.detailProofReport.detailProofImageIndices"));
      composition_review.insert(QStringLiteral("detailProofReport"), detail_report);
    }
  }
  projected.insert(QStringLiteral("compositionReview"), composition_review);

  QVariantList expected_graphic_ids;
  QSet<QString> unique_graphic_ids;
  bool graphic_ids_are_unambiguous = !graphic_layers.isEmpty();
  for (const auto& layer_value : graphic_layers) {
    const auto id_value = layer_value.toMap().value(QStringLiteral("id"));
    const auto id = id_value.toString().trimmed();
    if (id_value.metaType().id() != QMetaType::QString || id.isEmpty() ||
        unique_graphic_ids.contains(id)) {
      graphic_ids_are_unambiguous = false;
      break;
    }
    unique_graphic_ids.insert(id);
    expected_graphic_ids.append(id_value);
  }
  if (graphic_ids_are_unambiguous) {
    const auto replace_bound_id_lists = [&](auto&& self, QVariant& value) -> void {
      if (value.metaType().id() == QMetaType::QVariantMap) {
        auto map = value.toMap();
        const auto replace_list_with_reference = [&](const QString& key,
                                                     const QString& reference_key) {
          if (!map.contains(reference_key) &&
              map.value(key).metaType().id() == QMetaType::QVariantList &&
              map.value(key).toList() == expected_graphic_ids) {
            map.remove(key);
            map.insert(reference_key, true);
          }
        };
        replace_list_with_reference(QStringLiteral("rowIds"),
            QStringLiteral("rowIdsReference"));
        replace_list_with_reference(QStringLiteral("eligibleLayerIds"),
            QStringLiteral("eligibleLayerIdsReference"));
        replace_list_with_reference(QStringLiteral("observedLayerOrderIdsTopFirst"),
            QStringLiteral("observedLayerOrderIdsTopFirstReference"));
        for (auto iterator = map.begin(); iterator != map.end(); ++iterator)
          self(self, iterator.value());
        value = map;
      } else if (value.metaType().id() == QMetaType::QVariantList) {
        auto list = value.toList();
        for (auto& entry : list) self(self, entry);
        value = list;
      }
    };
    QVariant compacted = projected;
    replace_bound_id_lists(replace_bound_id_lists, compacted);
    projected = compacted.toMap();
  }

  auto photo_evidence = projected.value(QStringLiteral("photographicQualityEvidence")).toMap();
  if (photo_evidence.contains(QStringLiteral("introducedCompositeWarnings")) &&
      !photo_evidence.contains(QStringLiteral("introducedCompositeWarningsReference")) &&
      photo_evidence.value(QStringLiteral("currentCompositeWarnings")).metaType().id() ==
          QMetaType::QVariantList &&
      photo_evidence.value(QStringLiteral("introducedCompositeWarnings")) ==
          photo_evidence.value(QStringLiteral("currentCompositeWarnings"))) {
    photo_evidence.remove(QStringLiteral("introducedCompositeWarnings"));
    photo_evidence.insert(QStringLiteral("introducedCompositeWarningsReference"), true);
  }
  if (!photo_evidence.isEmpty())
    projected.insert(QStringLiteral("photographicQualityEvidence"), photo_evidence);
  if (projected.contains(QStringLiteral("compositeWarnings")) &&
      !projected.contains(QStringLiteral("compositeWarningsReference")) &&
      photo_evidence.value(QStringLiteral("currentCompositeWarnings")).metaType().id() ==
          QMetaType::QVariantList &&
      projected.value(QStringLiteral("compositeWarnings")) ==
          photo_evidence.value(QStringLiteral("currentCompositeWarnings"))) {
    projected.remove(QStringLiteral("compositeWarnings"));
    projected.insert(QStringLiteral("compositeWarningsReference"), true);
  }
  if (omit_unchanged_photo_distribution_bins) {
    auto scoped_photo_evidence = projected
        .value(QStringLiteral("photographicQualityEvidence")).toMap();
    QVariantList omitted_distribution_paths;
    for (const auto& stats_key : {QStringLiteral("baselineImageStats"),
             QStringLiteral("currentCompositeImageStats")}) {
      auto stats = scoped_photo_evidence.value(stats_key).toMap();
      for (const auto& key : {QStringLiteral("histogram"), QStringLiteral("vectorscope")}) {
        if (!stats.contains(key)) continue;
        stats.remove(key);
        omitted_distribution_paths.append(stats_key + QLatin1Char('.') + key);
      }
      if (!stats.isEmpty())
        scoped_photo_evidence.insert(stats_key, stats);
    }
    if (!omitted_distribution_paths.isEmpty()) {
      scoped_photo_evidence.insert(QStringLiteral("distributionEvidenceProjection"), QVariantMap{
          {QStringLiteral("contract"), QStringLiteral("photo-scalar-v1")},
          {QStringLiteral("basis"), QStringLiteral("host-bound-unchanged-photo")},
          {QStringLiteral("omitted"), omitted_distribution_paths},
          {QStringLiteral("nativeRetained"), true},
      });
      projected.insert(QStringLiteral("photographicQualityEvidence"), scoped_photo_evidence);
    }
  }
  return projected;
}

QJsonObject prompt_argument_guide_schema(const QJsonObject& schema) {
  QJsonObject guide;
  for (const auto& key : {QStringLiteral("type"), QStringLiteral("description"),
           QStringLiteral("required"), QStringLiteral("enum"), QStringLiteral("const"),
           QStringLiteral("additionalProperties"), QStringLiteral("minProperties"),
           QStringLiteral("maxProperties"), QStringLiteral("minItems"),
           QStringLiteral("maxItems"), QStringLiteral("uniqueItems")}) {
    if (schema.contains(key)) guide.insert(key, schema.value(key));
  }
  const auto properties = schema.value(QStringLiteral("properties")).toObject();
  if (!properties.isEmpty()) {
    QJsonObject compact_properties;
    for (auto it = properties.constBegin(); it != properties.constEnd(); ++it)
      compact_properties.insert(it.key(), prompt_argument_guide_schema(it.value().toObject()));
    guide.insert(QStringLiteral("properties"), compact_properties);
  }
  for (const auto& key : {QStringLiteral("items"), QStringLiteral("oneOf"),
           QStringLiteral("anyOf"), QStringLiteral("allOf"), QStringLiteral("if"),
           QStringLiteral("then"), QStringLiteral("else")}) {
    if (!schema.contains(key)) continue;
    const auto value = schema.value(key);
    if (value.isObject())
      guide.insert(key, prompt_argument_guide_schema(value.toObject()));
    else if (value.isArray()) {
      QJsonArray compact_values;
      for (const auto& child : value.toArray())
        compact_values.append(child.isObject()
            ? QJsonValue(prompt_argument_guide_schema(child.toObject())) : child);
      guide.insert(key, compact_values);
    }
  }
  return guide;
}

QVariantMap compact_composition_argument_guide(const QJsonObject& source_schema) {
  const auto properties = source_schema.value(QStringLiteral("properties")).toObject();
  QVariantMap guide{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("required"), source_schema.value(QStringLiteral("required")).toVariant()},
      {QStringLiteral("operationOptions"), properties.value(QStringLiteral("operation"))
           .toObject().value(QStringLiteral("enum")).toArray().toVariantList()},
      {QStringLiteral("familyFaceChoicesInResponseSchema"), true},
      {QStringLiteral("layerIdChoicesInResponseSchema"), true},
  };

  QVariantMap required_by_operation;
  const auto operation_schema = operation_specific_composition_schema(source_schema);
  for (const auto& branch_value : operation_schema.value(QStringLiteral("oneOf")).toArray()) {
    const auto branch = branch_value.toObject();
    const auto operation_values = branch.value(QStringLiteral("properties")).toObject()
        .value(QStringLiteral("operation")).toObject().value(QStringLiteral("enum")).toArray();
    if (operation_values.isEmpty() || !operation_values.first().isString()) continue;
    QStringList required_fields;
    for (const auto& field : branch.value(QStringLiteral("required")).toArray())
      if (field.isString() && field.toString() != QStringLiteral("operation"))
        required_fields.append(field.toString());
    const auto values = branch.value(QStringLiteral("properties")).toObject()
        .value(QStringLiteral("values")).toObject();
    for (const auto& field : values.value(QStringLiteral("required")).toArray())
      if (field.isString()) required_fields.append(QStringLiteral("values.%1").arg(field.toString()));
    required_by_operation.insert(operation_values.first().toString(), required_fields);
  }
  if (!required_by_operation.isEmpty())
    guide.insert(QStringLiteral("requiredFieldsByOperation"), required_by_operation);

  QVariantMap field_guide;
  const auto add_field = [&field_guide](
      const QString& path, const QJsonObject& field_schema, bool include_enum) {
    QVariantMap entry;
    for (const auto& key : {QStringLiteral("type"), QStringLiteral("description"),
             QStringLiteral("const")})
      if (field_schema.contains(key)) entry.insert(key, field_schema.value(key).toVariant());
    if (include_enum && field_schema.contains(QStringLiteral("enum")))
      entry.insert(QStringLiteral("enum"), field_schema.value(QStringLiteral("enum")).toArray().toVariantList());
    if (!entry.isEmpty()) field_guide.insert(path, entry);
  };
  for (auto it = properties.constBegin(); it != properties.constEnd(); ++it) {
    const auto field_schema = it.value().toObject();
    const auto path = it.key();
    if (path == QStringLiteral("values")) {
      const auto values = field_schema.value(QStringLiteral("properties")).toObject();
      for (auto value_it = values.constBegin(); value_it != values.constEnd(); ++value_it) {
        const auto value_schema = value_it.value().toObject();
        const auto enum_values = value_schema.value(QStringLiteral("enum")).toArray();
        add_field(QStringLiteral("values.%1").arg(value_it.key()), value_schema,
            !enum_values.isEmpty() && enum_values.size() <= 32);
      }
      continue;
    }
    const auto enum_values = field_schema.value(QStringLiteral("enum")).toArray();
    add_field(path, field_schema,
        path == QStringLiteral("operation") ||
        (!enum_values.isEmpty() && enum_values.size() <= 32 &&
         path != QStringLiteral("layerId")));
  }
  guide.insert(QStringLiteral("fieldSemantics"), field_guide);
  return guide;
}

QVariantList compact_tool_schemas_for_prompt(const QVariantList& tools) {
  QVariantList compact_tools;
  for (const auto& tool_value : tools) {
    const auto tool = tool_value.toMap();
    const auto name = tool.value(QStringLiteral("name")).toString().trimmed();
    auto schema = QJsonObject::fromVariantMap(
        tool.value(QStringLiteral("inputSchema")).toMap());
    if (name.isEmpty() || schema.isEmpty()) continue;
    if (name == QStringLiteral("photo.edit_composition"))
      schema = operation_specific_composition_schema(schema);
    const auto description = tool.value(QStringLiteral("description")).toString();
    // The constrained response format carries all generation constraints and
    // native option sets. This semantic guide keeps required fields, enum
    // choices, types, and every source description without repeating the
    // format/bounds boilerplate or annotations.
    QVariantMap compact_tool{
        {QStringLiteral("name"), name},
        {QStringLiteral("argumentGuide"),
         name == QStringLiteral("photo.edit_composition")
             ? compact_composition_argument_guide(
                   QJsonObject::fromVariantMap(tool.value(QStringLiteral("inputSchema")).toMap()))
             : prompt_argument_guide_schema(schema).toVariantMap()},
    };
    if (!description.isEmpty()) compact_tool.insert(QStringLiteral("description"), description);
    compact_tools.append(compact_tool);
  }
  return compact_tools;
}

QString current_adjustment_patch_for_prompt(const QString& patch_json) {
  QJsonParseError parse_error;
  auto document = QJsonDocument::fromJson(patch_json.toUtf8(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) return patch_json;
  auto root = document.object();
  auto state = root.value(QStringLiteral("adjustmentState")).toObject();
  const auto layers = state.value(QStringLiteral("layers"));
  const auto photo_layers = state.value(QStringLiteral("photoLayers"));
  if (layers.isArray() && photo_layers.isArray() && layers == photo_layers) {
    // Some recipe snapshots expose these two keys as identical compatibility
    // aliases. Keep the canonical `layers` value and omit only the exact alias.
    state.remove(QStringLiteral("photoLayers"));
    root.insert(QStringLiteral("adjustmentState"), state);
    document.setObject(root);
  }
  return QString::fromUtf8(document.toJson(QJsonDocument::Compact));
}

QString user_instructions_for_prompt(const AgentLocalModelPlanRequest& request) {
  const auto active_instructions = request.user_instructions.trimmed();
  if (active_instructions.isEmpty()) return request.user_instructions;
  QString latest_approved_instructions;
  for (const auto& raw_value : request.prior_tool_results) {
    const auto value = raw_value.toMap();
    if (value.value(QStringLiteral("kind")).toString() !=
            QStringLiteral("approved-task-snapshot") ||
        value.value(QStringLiteral("authority")).toString() !=
            QStringLiteral("approved-brief-and-deliverables"))
      continue;
    latest_approved_instructions = value.value(QStringLiteral("snapshot")).toMap()
        .value(QStringLiteral("approvedInstructions")).toString().trimmed();
  }
  if (!latest_approved_instructions.isEmpty() &&
      latest_approved_instructions == active_instructions) {
    return QStringLiteral(
        "Use the identical approvedInstructions in the latest frozen approved-task snapshot in Prior MCP tool results JSON.");
  }
  return request.user_instructions;
}

QVariantMap compact_tool_call_validation_recovery(
    const QVariantMap& envelope) {
  auto compact = envelope;
  auto validation = envelope.value(QStringLiteral("toolCallValidation")).toMap();
  QVariantList rejected_calls;
  const auto original_rejected_calls = validation.value(QStringLiteral("rejectedCalls")).toList();
  for (const auto& rejected_value : original_rejected_calls) {
    auto rejected = rejected_value.toMap();
    // The current required tool catalog and operation-specific generation
    // schema are already sent in this request. Copying the rejected call's
    // validation schema here duplicates that catalog for every failed call
    // and can push recovery prompts beyond the model context window.
    rejected.remove(QStringLiteral("inputSchema"));
    rejected_calls.append(rejected);
  }
  validation.insert(QStringLiteral("rejectedCalls"), rejected_calls);
  compact.insert(QStringLiteral("toolCallValidation"), validation);
  // The unexecuted calls are preserved inside toolCallValidation. Avoid
  // sending their identical top-level copy as well; the full envelope remains
  // available in the controller's durable audit receipt.
  compact.remove(QStringLiteral("unexecutedCalls"));
  compact.remove(QStringLiteral("currentToolSchemas"));
  compact.insert(QStringLiteral("toolSchemasProvidedSeparately"), true);
  return compact;
}

// Composition replies include complete UI layer rows and duplicated transport
// envelopes. Retain the operation history and latest editor state instead of
// allowing that metadata to evict every returned ID and execution error.
bool is_approved_task_snapshot_envelope(const QVariantMap& value) {
  const auto snapshot = value.value(QStringLiteral("snapshot")).toMap();
  return value.value(QStringLiteral("kind")).toString() ==
             QStringLiteral("approved-task-snapshot") &&
         value.value(QStringLiteral("authority")).toString() ==
             QStringLiteral("approved-brief-and-deliverables") &&
         !snapshot.value(QStringLiteral("approvedInstructions")).toString().trimmed().isEmpty();
}

QVariantMap compact_approved_task_snapshot(const QVariantMap& value) {
  const auto snapshot = value.value(QStringLiteral("snapshot")).toMap();
  QVariantMap compact_snapshot;
  for (const auto& key : {QStringLiteral("approvedInstructions"), QStringLiteral("exactCopy"),
           QStringLiteral("deliverables"), QStringLiteral("targetIds"),
           QStringLiteral("taskScopedCapabilities")}) {
    if (!snapshot.contains(key)) continue;
    const auto source = snapshot.value(key);
    compact_snapshot.insert(key, key == QStringLiteral("approvedInstructions")
        ? source : compact_prompt_evidence(source, key));
  }
  return QVariantMap{
      {QStringLiteral("kind"), QStringLiteral("approved-task-snapshot")},
      {QStringLiteral("authority"), QStringLiteral("approved-brief-and-deliverables")},
      {QStringLiteral("snapshot"), compact_snapshot},
  };
}

QVariantList compact_font_face_inventory(
    const QVariantList& inventory,
    qsizetype* omitted_family_count,
    qsizetype* omitted_face_count) {
  constexpr qsizetype kMaximumFamilies = 48;
  constexpr qsizetype kMaximumFacesPerFamily = 8;
  constexpr qsizetype kMaximumNameLength = 160;
  constexpr qsizetype kMaximumInventoryBytes = 12000;
  QVariantList compact_inventory;
  qsizetype serialized_bytes = 2;
  qsizetype omitted_families = 0;
  qsizetype omitted_faces = 0;
  for (qsizetype index = 0; index < inventory.size(); ++index) {
    const auto entry = inventory.at(index).toMap();
    const auto family = entry.value(QStringLiteral("family")).toString();
    if (family.trimmed().isEmpty() || family.size() > kMaximumNameLength) {
      ++omitted_families;
      continue;
    }
    QStringList faces;
    const auto raw_faces = entry.value(QStringLiteral("faces")).toList();
    for (const auto& raw_face : raw_faces) {
      const auto face = raw_face.toString();
      if (face.trimmed().isEmpty() || face.size() > kMaximumNameLength) {
        ++omitted_faces;
        continue;
      }
      if (faces.size() >= kMaximumFacesPerFamily) {
        ++omitted_faces;
        continue;
      }
      if (!faces.contains(face)) faces.append(face);
    }
    if (faces.isEmpty()) {
      ++omitted_families;
      continue;
    }
    const QVariantMap compact_entry{
        {QStringLiteral("family"), family},
        {QStringLiteral("faces"), faces},
    };
    const auto entry_bytes = QJsonDocument::fromVariant(compact_entry)
                                 .toJson(QJsonDocument::Compact).size();
    if (compact_inventory.size() >= kMaximumFamilies ||
        serialized_bytes + entry_bytes + 1 > kMaximumInventoryBytes) {
      omitted_families += inventory.size() - index;
      break;
    }
    serialized_bytes += entry_bytes + (compact_inventory.isEmpty() ? 0 : 1);
    compact_inventory.append(compact_entry);
  }
  if (omitted_family_count != nullptr) *omitted_family_count = omitted_families;
  if (omitted_face_count != nullptr) *omitted_face_count = omitted_faces;
  return compact_inventory;
}

QVariantMap compact_revision_baseline(const QVariantMap& revision_baseline) {
  if (revision_baseline.isEmpty()) return {};
  QVariantMap compact_baseline;
  for (const auto& key : {QStringLiteral("source"), QStringLiteral("assetId"),
           QStringLiteral("sourceProofSha256"), QStringLiteral("sourceRecipeSha256")})
    if (revision_baseline.contains(key)) compact_baseline.insert(key, revision_baseline.value(key));
  if (revision_baseline.contains(QStringLiteral("layerIdsTopFirst"))) {
    QVariantList layer_ids;
    const auto source_ids = revision_baseline.value(QStringLiteral("layerIdsTopFirst")).toList();
    for (qsizetype i = 0; i < std::min<qsizetype>(source_ids.size(), 12); ++i)
      if (source_ids.at(i).typeId() == QMetaType::QString) layer_ids.append(source_ids.at(i));
    compact_baseline.insert(QStringLiteral("layerIdsTopFirst"), layer_ids);
  }
  QVariantList text_layers;
  const auto source_text_layers = revision_baseline.value(QStringLiteral("textLayers")).toList();
  for (qsizetype i = 0; i < std::min<qsizetype>(source_text_layers.size(), 12); ++i) {
    const auto source = source_text_layers.at(i).toMap();
    QVariantMap layer;
    for (const auto& key : {QStringLiteral("id"), QStringLiteral("text"),
             QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("scale"),
             QStringLiteral("rotation"), QStringLiteral("layerIndex")})
      if (source.contains(key)) layer.insert(key, compact_prompt_evidence(source.value(key), key));
    if (source.contains(QStringLiteral("textTypography")))
      layer.insert(QStringLiteral("textTypography"), compact_prompt_evidence(
          source.value(QStringLiteral("textTypography")), QStringLiteral("textTypography")));
    text_layers.append(layer);
  }
  compact_baseline.insert(QStringLiteral("textLayers"), text_layers);
  return compact_baseline;
}

QVariantMap compact_current_composition_assessment(const QVariantMap& value) {
  if (value.value(QStringLiteral("kind")).toString() !=
          QStringLiteral("current-composition-assessment") ||
      value.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-current-composition-assessment-v1"))
    return {};

  QVariantMap compact_assessment;
  for (const auto& key : {QStringLiteral("kind"), QStringLiteral("contract"),
           QStringLiteral("assetId"), QStringLiteral("batchRound"),
           QStringLiteral("sourceProofSha256"), QStringLiteral("sourceRecipeSha256"),
           QStringLiteral("requireProductivePlan"), QStringLiteral("instruction")})
    if (value.contains(key)) compact_assessment.insert(key, value.value(key));

  const auto compact_string_list = [](const QVariant& source) {
    QVariantList compact;
    const auto values = source.toList();
    for (qsizetype i = 0; i < std::min<qsizetype>(values.size(), 6); ++i)
      if (values.at(i).typeId() == QMetaType::QString)
        compact.append(values.at(i));
    return compact;
  };
  if (value.contains(QStringLiteral("textLayerIds"))) {
    QVariantList text_layer_ids;
    const auto ids = value.value(QStringLiteral("textLayerIds")).toList();
    for (qsizetype i = 0; i < std::min<qsizetype>(ids.size(), 8); ++i)
      if (ids.at(i).typeId() == QMetaType::QString) text_layer_ids.append(ids.at(i));
    compact_assessment.insert(QStringLiteral("textLayerIds"), text_layer_ids);
  }

  const auto native_review = value.value(QStringLiteral("nativeReview")).toMap();
  if (!native_review.isEmpty()) {
    QVariantMap compact_native_review;
    for (const auto& key : {QStringLiteral("ok"), QStringLiteral("complete"),
             QStringLiteral("requiredHierarchyAvailable"), QStringLiteral("hierarchyPassed")})
      if (native_review.contains(key)) compact_native_review.insert(key, native_review.value(key));
    for (const auto& key : {QStringLiteral("missingExpectedCopy"),
             QStringLiteral("duplicateExpectedCopy"), QStringLiteral("warnings")})
      if (native_review.contains(key))
        compact_native_review.insert(key, compact_string_list(native_review.value(key)));

    const auto typography = native_review.value(QStringLiteral("typography")).toMap();
    if (!typography.isEmpty()) {
      QVariantMap compact_typography;
      for (const auto& key : {QStringLiteral("explicitFamilyFaceRequired"),
               QStringLiteral("requiredHierarchyAvailable"), QStringLiteral("hierarchyAvailable"),
               QStringLiteral("hierarchyPassed"), QStringLiteral("minimumRatio")})
        if (typography.contains(key)) compact_typography.insert(key, typography.value(key));
      if (typography.contains(QStringLiteral("missingFamilyFaceCopy")))
        compact_typography.insert(QStringLiteral("missingFamilyFaceCopy"),
            compact_string_list(typography.value(QStringLiteral("missingFamilyFaceCopy"))));
      QVariantList family_face_readback;
      const auto source_readback = typography.value(QStringLiteral("familyFaceReadback")).toList();
      const QStringList family_face_keys{QStringLiteral("text"), QStringLiteral("layerId"),
          QStringLiteral("family"), QStringLiteral("face"), QStringLiteral("familyAvailable"),
          QStringLiteral("faceAvailable")};
      for (qsizetype i = 0; i < std::min<qsizetype>(source_readback.size(), 8); ++i) {
        const auto source = source_readback.at(i).toMap();
        QVariantMap entry;
        for (const auto& key : family_face_keys)
          if (source.contains(key)) entry.insert(key, compact_prompt_evidence(source.value(key), key));
        family_face_readback.append(entry);
      }
      if (!family_face_readback.isEmpty())
        compact_typography.insert(QStringLiteral("familyFaceReadback"), family_face_readback);
      const auto hierarchy = typography.value(QStringLiteral("hierarchy")).toMap();
      if (!hierarchy.isEmpty()) {
        QVariantMap compact_hierarchy;
        for (const auto& key : {QStringLiteral("headlineEffectiveSize"),
                 QStringLiteral("kickerEffectiveSize"), QStringLiteral("footerEffectiveSize"),
                 QStringLiteral("minimumRatio")})
          if (hierarchy.contains(key)) compact_hierarchy.insert(key, hierarchy.value(key));
        compact_typography.insert(QStringLiteral("hierarchy"), compact_hierarchy);
      }
      const auto fit = typography.value(QStringLiteral("layoutFitGuidance")).toMap();
      if (!fit.isEmpty()) {
        QVariantMap compact_fit;
        for (const auto& key : {QStringLiteral("available"), QStringLiteral("headlineLayerId"),
                 QStringLiteral("currentRenderedWidth"), QStringLiteral("proposedSymmetricMargin"),
                 QStringLiteral("availableWidthAtProposedMargins"), QStringLiteral("proportionalWidthScaleProxy"),
                 QStringLiteral("headlineEffectiveSizeProxy"), QStringLiteral("smallerRoleEffectiveSizeUpperProxy"),
                 QStringLiteral("approximationOnly"), QStringLiteral("basis"), QStringLiteral("strategy")})
          if (fit.contains(key)) compact_fit.insert(key, fit.value(key));
        compact_typography.insert(QStringLiteral("layoutFitGuidance"), compact_fit);
      }
      compact_native_review.insert(QStringLiteral("typography"), compact_typography);
    }
    const auto hierarchy = native_review.value(QStringLiteral("hierarchy")).toMap();
    if (!hierarchy.isEmpty()) {
      QVariantMap compact_hierarchy;
      for (const auto& key : {QStringLiteral("headlineEffectiveSize"),
               QStringLiteral("kickerEffectiveSize"), QStringLiteral("footerEffectiveSize"),
               QStringLiteral("minimumRatio")})
        if (hierarchy.contains(key)) compact_hierarchy.insert(key, hierarchy.value(key));
      compact_native_review.insert(QStringLiteral("hierarchy"), compact_hierarchy);
    }
    compact_assessment.insert(QStringLiteral("nativeReview"), compact_native_review);
  }

  QVariantList compact_geometry_checks;
  const auto geometry_checks = value.value(QStringLiteral("geometryChecks")).toList();
  const QStringList geometry_keys{QStringLiteral("layerId"), QStringLiteral("available"),
      QStringLiteral("rasterFrameWithinCanvas"), QStringLiteral("boundsBasis"),
      QStringLiteral("transformAvailable"), QStringLiteral("layoutError"),
      QStringLiteral("layerScale"), QStringLiteral("layerRotation"),
      QStringLiteral("effectiveTypographySize"), QStringLiteral("canvasWidth"),
      QStringLiteral("canvasHeight")};
  for (qsizetype i = 0; i < std::min<qsizetype>(geometry_checks.size(), 6); ++i) {
    const auto source = geometry_checks.at(i).toMap();
    QVariantMap entry;
    for (const auto& key : geometry_keys)
      if (source.contains(key)) entry.insert(key, compact_prompt_evidence(source.value(key), key));
    const auto source_bounds = source.value(QStringLiteral("renderedBounds")).toMap();
    if (!source_bounds.isEmpty()) {
      QVariantMap bounds;
      for (const auto& key : {QStringLiteral("x"), QStringLiteral("y"),
               QStringLiteral("w"), QStringLiteral("h"),
               QStringLiteral("width"), QStringLiteral("height")})
        if (source_bounds.contains(key)) bounds.insert(key, source_bounds.value(key));
      entry.insert(QStringLiteral("renderedBounds"), bounds);
    }
    compact_geometry_checks.append(entry);
  }
  if (!compact_geometry_checks.isEmpty())
    compact_assessment.insert(QStringLiteral("geometryChecks"), compact_geometry_checks);

  const auto revision_baseline = compact_revision_baseline(
      value.value(QStringLiteral("revisionBaseline")).toMap());
  if (!revision_baseline.isEmpty())
    compact_assessment.insert(QStringLiteral("revisionBaseline"), revision_baseline);

  const auto model_critique = value.value(QStringLiteral("modelCritique")).toMap();
  if (!model_critique.isEmpty()) {
    QVariantMap compact_critique;
    for (const auto& key : {QStringLiteral("available"), QStringLiteral("ok"),
             QStringLiteral("pass"), QStringLiteral("needsUserReview")})
      if (model_critique.contains(key)) compact_critique.insert(key, model_critique.value(key));
    for (const auto& key : {QStringLiteral("issues"), QStringLiteral("repairHints")}) {
      if (!model_critique.contains(key)) continue;
      QVariantList entries;
      const auto source_entries = model_critique.value(key).toList();
      for (qsizetype i = 0; i < std::min<qsizetype>(source_entries.size(), 6); ++i)
        entries.append(compact_prompt_evidence(source_entries.at(i), key));
      compact_critique.insert(key, entries);
    }
    compact_assessment.insert(QStringLiteral("modelCritique"), compact_critique);
  }
  return compact_assessment;
}

bool is_proof_or_validation_receipt(const QVariantMap& value) {
  auto name = value.value(QStringLiteral("name")).toString();
  if (name.isEmpty()) name = value.value(QStringLiteral("toolName")).toString();
  name = name.toLower();
  if (name.contains(QStringLiteral("proof")) || name.contains(QStringLiteral("review")) ||
      name.contains(QStringLiteral("validat")))
    return true;
  auto result = value.value(QStringLiteral("result")).toMap();
  if (result.isEmpty()) result = value;
  const auto content = result.value(QStringLiteral("structuredContent")).toMap();
  for (const auto& key : {QStringLiteral("imageStats"), QStringLiteral("compositionReview"),
           QStringLiteral("proofReport"), QStringLiteral("validationReport"),
           QStringLiteral("detailProofReport"), QStringLiteral("proofImageSha256")})
    if (content.contains(key) || result.contains(key)) return true;
  return false;
}

QVariantMap compact_proof_receipt(const QVariantMap& value) {
  auto result = value.value(QStringLiteral("result")).toMap();
  if (result.isEmpty()) result = value;
  QVariantMap compact_result;
  for (const auto& key : {QStringLiteral("ok"), QStringLiteral("error"),
           QStringLiteral("toolExecutionError"), QStringLiteral("pass"),
           QStringLiteral("needsUserReview"), QStringLiteral("summary"),
           QStringLiteral("proofImageSha256"), QStringLiteral("renderedCompositionWidth"),
           QStringLiteral("renderedCompositionHeight")})
    if (result.contains(key)) compact_result.insert(key, compact_prompt_evidence(result.value(key), key));
  auto content = result.value(QStringLiteral("structuredContent")).toMap();
  if (content.isEmpty()) content = result;
  QVariantMap compact_content;
  const QStringList proof_keys{
      QStringLiteral("ok"), QStringLiteral("error"), QStringLiteral("pass"),
      QStringLiteral("needsUserReview"), QStringLiteral("summary"),
      QStringLiteral("issues"), QStringLiteral("repairHints"), QStringLiteral("warnings"),
      QStringLiteral("proofImageSha256"), QStringLiteral("renderedCompositionWidth"),
      QStringLiteral("renderedCompositionHeight"), QStringLiteral("imageStats"),
      QStringLiteral("compositionReview"), QStringLiteral("proofReport"),
      QStringLiteral("validationReport"), QStringLiteral("detailProofReport"),
      QStringLiteral("visualEvidence")};
  for (const auto& key : proof_keys) {
    if (!content.contains(key)) continue;
    const auto candidate = compact_prompt_evidence(content.value(key), key);
    if (QJsonDocument::fromVariant(candidate).toJson(QJsonDocument::Compact).size() > 3000)
      continue;
    compact_content.insert(key, candidate);
    const auto content_bytes = QJsonDocument::fromVariant(compact_content)
                                   .toJson(QJsonDocument::Compact).size();
    if (content_bytes > 7000) compact_content.remove(key);
  }
  if (!compact_content.isEmpty()) compact_result.insert(QStringLiteral("structuredContent"), compact_content);
  const auto call_name = value.value(QStringLiteral("name"), value.value(QStringLiteral("toolName"))).toString();
  QVariantMap compact_receipt{{QStringLiteral("name"), call_name},
      {QStringLiteral("result"), compact_result}};
  const auto arguments = value.value(QStringLiteral("arguments")).toMap();
  QVariantMap compact_arguments;
  for (const auto& key : {QStringLiteral("assetId"), QStringLiteral("assetIds"),
           QStringLiteral("layerId"), QStringLiteral("layerIds"), QStringLiteral("commandId")})
    if (arguments.contains(key)) compact_arguments.insert(key, arguments.value(key));
  if (!compact_arguments.isEmpty()) compact_receipt.insert(QStringLiteral("arguments"), compact_arguments);
  for (const auto& key : {QStringLiteral("callId"), QStringLiteral("toolCallId"),
           QStringLiteral("originalCallId")}) {
    const auto id = result.value(key, value.value(key)).toString();
    if (!id.isEmpty()) compact_receipt.insert(key, id.left(160));
  }
  return compact_receipt;
}

QVariantMap compact_current_composition_details(QVariantMap current) {
  auto current_result = current.value(QStringLiteral("result")).toMap();
  auto current_content = current_result.value(QStringLiteral("structuredContent")).toMap();
  const auto source_layers = current_content.value(QStringLiteral("layers")).toList();
  auto current_layers = source_layers.mid(0, std::min<qsizetype>(source_layers.size(), 32));
  const QStringList text_typography_keys{
      QStringLiteral("family"), QStringLiteral("face"), QStringLiteral("size"),
      QStringLiteral("weight"), QStringLiteral("italic"), QStringLiteral("underline"),
      QStringLiteral("color"), QStringLiteral("tracking"), QStringLiteral("lineHeight"),
      QStringLiteral("strokeWidth"), QStringLiteral("strokeColor"), QStringLiteral("shadowX"),
      QStringLiteral("shadowY"), QStringLiteral("shadowColor"), QStringLiteral("mode"),
      QStringLiteral("paragraphSpacing"), QStringLiteral("axes"), QStringLiteral("pathPoints"),
      QStringLiteral("pathContours"), QStringLiteral("pathStart"), QStringLiteral("pathEnd"),
      QStringLiteral("pathOffset"), QStringLiteral("pathSpacing"), QStringLiteral("pathSide"),
      QStringLiteral("pathClosed"), QStringLiteral("pathExclusion"), QStringLiteral("material"),
      QStringLiteral("materialTint"), QStringLiteral("materialBlur"),
      QStringLiteral("materialTransmission"), QStringLiteral("materialEdgeSoftness"),
      QStringLiteral("materialDepth"), QStringLiteral("materialLightAngle")};
  for (auto& layer_value : current_layers) {
    auto layer = layer_value.toMap();
    layer.remove(QStringLiteral("effects"));
    layer.remove(QStringLiteral("displayReferredTextEligibilityReason"));
    const auto typography = layer.value(QStringLiteral("textTypography")).toMap();
    if (!typography.isEmpty()) {
      QVariantMap compact_typography;
      for (const auto& key : text_typography_keys)
        if (typography.contains(key)) compact_typography.insert(key, typography.value(key));
      layer.insert(QStringLiteral("textTypography"), compact_typography);
    }
    layer_value = layer;
  }
  current_content.insert(QStringLiteral("layers"), current_layers);
  const auto prior_omitted_layers = static_cast<qsizetype>(
      current_content.value(QStringLiteral("layersOmittedCount")).toLongLong());
  const qsizetype omitted_layers = prior_omitted_layers + source_layers.size() - current_layers.size();
  if (omitted_layers > 0)
    current_content.insert(QStringLiteral("layersOmittedCount"), omitted_layers);
  auto compact_masks = current_content.value(QStringLiteral("masks")).toList();
  const auto source_mask_count = compact_masks.size();
  if (compact_masks.size() > 64) compact_masks = compact_masks.mid(0, 64);
  for (auto& mask_value : compact_masks) {
    auto mask = mask_value.toMap();
    QVariantMap mask_identity;
    for (const auto& key : {QStringLiteral("id"), QStringLiteral("name"),
             QStringLiteral("type"), QStringLiteral("enabled"), QStringLiteral("locked")})
      if (mask.contains(key)) mask_identity.insert(key, mask.value(key));
    mask_value = mask_identity;
  }
  current_content.insert(QStringLiteral("masks"), compact_masks);
  const qsizetype omitted_masks = static_cast<qsizetype>(
      current_content.value(QStringLiteral("masksOmittedCount")).toLongLong()) +
      source_mask_count - compact_masks.size();
  if (omitted_masks > 0) current_content.insert(QStringLiteral("masksOmittedCount"), omitted_masks);
  current_result.insert(QStringLiteral("structuredContent"), current_content);
  current.insert(QStringLiteral("result"), current_result);
  return current;
}

QString prior_tool_results_prompt_json(const QVariantList& raw_values) {
  constexpr qsizetype kMaximumPriorToolHistoryBytes = 32000;
  QVariantMap latest_approved_task_snapshot;
  QVariantMap latest_current_composition_assessment;
  QVariantMap latest_revision_baseline;
  for (const auto& raw_value : raw_values) {
    const auto value = raw_value.toMap();
    if (is_approved_task_snapshot_envelope(value)) latest_approved_task_snapshot = value;
    if (value.value(QStringLiteral("kind")).toString() == QStringLiteral("draft-revision")) {
      const auto context = value.value(QStringLiteral("context")).toMap();
      if (context.contains(QStringLiteral("revisionCompositionBaseline")))
        latest_revision_baseline = compact_revision_baseline(
            context.value(QStringLiteral("revisionCompositionBaseline")).toMap());
    }
    if (value.value(QStringLiteral("kind")).toString() ==
        QStringLiteral("current-composition-assessment")) {
      const auto assessment = compact_current_composition_assessment(value);
      if (!assessment.isEmpty()) {
        latest_current_composition_assessment = assessment;
        if (assessment.contains(QStringLiteral("revisionBaseline")))
          latest_revision_baseline = assessment.value(QStringLiteral("revisionBaseline")).toMap();
      }
    }
  }
  QVariantList approved_task_snapshots;
  if (!latest_approved_task_snapshot.isEmpty())
    approved_task_snapshots.append(compact_approved_task_snapshot(latest_approved_task_snapshot));
  QVariantList values;
  for (const auto& raw_value : raw_values) {
    const auto value = raw_value.toMap();
    if (value.value(QStringLiteral("kind")).toString() ==
        QStringLiteral("current-composition-assessment"))
      continue;
    if (value.value(QStringLiteral("kind")).toString() ==
            QStringLiteral("approved-task-snapshot") &&
        !is_approved_task_snapshot_envelope(value)) {
      continue;
    }
    // Live controller reports have toolName and result fields at the top level;
    // direct planner clients use a name/arguments/result envelope.
    if (value.contains(QStringLiteral("toolName")) && !value.contains(QStringLiteral("result"))) {
      values.append(QVariantMap{{QStringLiteral("name"), value.value(QStringLiteral("toolName"))},
          {QStringLiteral("arguments"), value.value(QStringLiteral("arguments"))},
          {QStringLiteral("result"), value}});
    } else {
      values.append(value);
    }
  }
  QVariantList results;
  QVariantList validation_recoveries;
  QVariantList latest_native_font_faces;
  QVariantMap latest_font_probe;
  qsizetype latest_composition = -1;
  for (qsizetype i = 0; i < values.size(); ++i) {
    const auto value = values.at(i).toMap();
    const auto name = value.value(QStringLiteral("name")).toString();
    if (name == QStringLiteral("photo.inspect_composition") || name == QStringLiteral("photo.edit_composition")) {
      const auto tool_result = value.value(QStringLiteral("result")).toMap();
      const auto content = tool_result.value(QStringLiteral("structuredContent")).toMap();
      if (tool_result.value(QStringLiteral("ok")).toBool() &&
          content.contains(QStringLiteral("layers")))
        latest_composition = i;
      const auto arguments = value.value(QStringLiteral("arguments")).toMap();
      const auto requested_values = arguments.value(QStringLiteral("values")).toMap();
      const auto font_faces = content.value(QStringLiteral("fontFaces")).toList();
      const auto initial_inventory_read =
          name == QStringLiteral("photo.inspect_composition") && requested_values.isEmpty();
      const auto mutation_inventory_read = name == QStringLiteral("photo.edit_composition");
      if (tool_result.value(QStringLiteral("ok")).toBool() &&
          (initial_inventory_read || mutation_inventory_read) && !font_faces.isEmpty())
        latest_native_font_faces = font_faces;
      if (name == QStringLiteral("photo.inspect_composition") &&
          tool_result.value(QStringLiteral("ok")).toBool() && !requested_values.isEmpty()) {
        QVariantMap font_probe;
        for (const auto& key : {QStringLiteral("fontFamily"), QStringLiteral("fontFace"),
                 QStringLiteral("requestedFamilyFaces"), QStringLiteral("fontFamilyAvailable"),
                 QStringLiteral("fontFaceAvailable"), QStringLiteral("fontInspectionRequested"),
                 QStringLiteral("fontAxes")}) {
          if (content.contains(key)) font_probe.insert(key, content.value(key));
        }
        if (!font_probe.isEmpty()) latest_font_probe = font_probe;
      }
    }
  }
  const QStringList layer_keys{
      QStringLiteral("id"), QStringLiteral("kind"), QStringLiteral("name"), QStringLiteral("parentId"),
      QStringLiteral("position"), QStringLiteral("enabled"), QStringLiteral("locked"), QStringLiteral("opacity"),
      QStringLiteral("blendMode"), QStringLiteral("clippingMask"), QStringLiteral("selectionScoped"),
      QStringLiteral("selectionMaskSetId"), QStringLiteral("selectionMaskSetIds"), QStringLiteral("maskEnabled"),
      QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("scale"), QStringLiteral("rotation"),
      QStringLiteral("canvasWidth"), QStringLiteral("canvasHeight"), QStringLiteral("pixelWidth"),
      QStringLiteral("pixelHeight"), QStringLiteral("text"), QStringLiteral("textTypography"), QStringLiteral("effects"),
      QStringLiteral("renderColorDomain"), QStringLiteral("displayReferredTextEligibility"),
      QStringLiteral("displayReferredTextEligibilityReason"), QStringLiteral("displayReferredTextPolicyVersion")};
  for (qsizetype i = 0; i < values.size(); ++i) {
    const auto value = values.at(i).toMap();
    const auto name = value.value(QStringLiteral("name")).toString();
    if (value.value(QStringLiteral("kind")).toString() ==
        QStringLiteral("tool-call-validation-recovery")) {
      const auto compact_recovery = compact_tool_call_validation_recovery(value);
      results.append(compact_recovery);
      validation_recoveries.append(compact_recovery);
      continue;
    }
    if (name != QStringLiteral("photo.inspect_composition") && name != QStringLiteral("photo.edit_composition") &&
        name != QStringLiteral("app.invoke_command")) {
      results.append(compact_prompt_evidence(value));
      continue;
    }
    const auto original = value.value(QStringLiteral("result")).toMap();
    const auto content = original.value(QStringLiteral("structuredContent")).toMap();
    QVariantMap result;
    for (const auto& key : {QStringLiteral("ok"), QStringLiteral("error"), QStringLiteral("toolExecutionError")})
      if (original.contains(key)) result.insert(key, original.value(key));
    for (const auto& key : {QStringLiteral("callId"), QStringLiteral("toolCallId"),
             QStringLiteral("originalCallId"), QStringLiteral("id")}) {
      if (original.contains(key)) result.insert(key, original.value(key));
      else if (value.contains(key)) result.insert(key, value.value(key));
    }
    QVariantMap compact_content;
    for (const auto& key : {QStringLiteral("ok"), QStringLiteral("error"), QStringLiteral("changed"),
             QStringLiteral("recoverableError"), QStringLiteral("mutationNotApplied"), QStringLiteral("errorCode"),
             QStringLiteral("recoveryRequired"),
             QStringLiteral("replayPrevented"), QStringLiteral("alreadyCompleted"),
             QStringLiteral("alreadySatisfied"),
             QStringLiteral("taskStateHash"),
             QStringLiteral("createdLayerId"), QStringLiteral("createdLayerIds"), QStringLiteral("createdMaskId"), QStringLiteral("effectId"),
             QStringLiteral("createdClipId"), QStringLiteral("createdClipIds"), QStringLiteral("resourceId"),
             QStringLiteral("priorReturnedIds"),
             QStringLiteral("paintBusy"), QStringLiteral("paintError")})
      if (content.contains(key)) compact_content.insert(key, content.value(key));
    const auto arguments = value.value(QStringLiteral("arguments")).toMap();
    if (!compact_content.contains(QStringLiteral("createdLayerId")) &&
        arguments.value(QStringLiteral("operation")).toString() == QStringLiteral("add_text") &&
        original.value(QStringLiteral("ok")).toBool() && !content.value(QStringLiteral("layers")).toList().isEmpty())
      compact_content.insert(QStringLiteral("createdLayerId"),
          content.value(QStringLiteral("layers")).toList().first().toMap().value(QStringLiteral("id")));
    if (i == latest_composition) {
      for (const auto& key : {QStringLiteral("assetId"), QStringLiteral("width"), QStringLiteral("height"),
               QStringLiteral("order"), QStringLiteral("masks"), QStringLiteral("fontAxes"),
               QStringLiteral("fontFamily"), QStringLiteral("fontFace"), QStringLiteral("fontFamilyAvailable"),
               QStringLiteral("fontFaceAvailable"), QStringLiteral("fontInspectionRequested"),
               QStringLiteral("requestedFamilyFaces"),
               QStringLiteral("displayTextColorPolicy"), QStringLiteral("photoFragmentBrushPresets")})
        if (content.contains(key)) compact_content.insert(key, compact_prompt_evidence(content.value(key), key));
      const auto source_font_faces = latest_native_font_faces.isEmpty()
          ? content.value(QStringLiteral("fontFaces")).toList()
          : latest_native_font_faces;
      qsizetype omitted_font_families = 0;
      qsizetype omitted_font_faces = 0;
      const auto compact_font_faces = compact_font_face_inventory(
          source_font_faces, &omitted_font_families, &omitted_font_faces);
      if (!compact_font_faces.isEmpty()) {
        compact_content.insert(QStringLiteral("fontFaces"), compact_font_faces);
        if (omitted_font_families > 0)
          compact_content.insert(QStringLiteral("fontFacesOmittedFamilyCount"), omitted_font_families);
        if (omitted_font_faces > 0)
          compact_content.insert(QStringLiteral("fontFacesOmittedFaceCount"), omitted_font_faces);
      } else if (content.contains(QStringLiteral("fonts"))) {
        compact_content.insert(QStringLiteral("fonts"),
            compact_prompt_evidence(content.value(QStringLiteral("fonts")), QStringLiteral("fonts")));
      }
      for (auto it = latest_font_probe.cbegin(); it != latest_font_probe.cend(); ++it)
        compact_content.insert(it.key(), compact_prompt_evidence(it.value(), it.key()));
      QVariantList layers;
      const auto source_layers = content.value(QStringLiteral("layers")).toList();
      for (qsizetype layer_index = 0;
           layer_index < std::min<qsizetype>(source_layers.size(), 64); ++layer_index) {
        const auto& layer_value = source_layers.at(layer_index);
        const auto layer = layer_value.toMap();
        QVariantMap compact_layer;
        for (const auto& key : layer_keys)
          if (layer.contains(key)) compact_layer.insert(key, compact_prompt_evidence(layer.value(key), key));
        layers.append(compact_layer);
      }
      if (source_layers.size() > layers.size())
        compact_content.insert(QStringLiteral("layersOmittedCount"), source_layers.size() - layers.size());
      compact_content.insert(QStringLiteral("layers"), layers);
    }
    result.insert(QStringLiteral("structuredContent"), compact_content);
    results.append(QVariantMap{{QStringLiteral("name"), name},
        {QStringLiteral("arguments"), compact_prompt_evidence(arguments)}, {QStringLiteral("result"), result}});
  }
  qsizetype successful_native_mutation_count = 0;
  for (const auto& result_value : results) {
    const auto item = result_value.toMap();
    const auto name = item.value(QStringLiteral("name")).toString();
    const auto arguments = item.value(QStringLiteral("arguments")).toMap();
    const auto outcome = item.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("structuredContent")).toMap();
    if ((name == QStringLiteral("photo.edit_composition") ||
         name == QStringLiteral("app.invoke_command")) &&
        arguments.value(QStringLiteral("operation")).toString() != QStringLiteral("inspect") &&
        item.value(QStringLiteral("result")).toMap().value(QStringLiteral("ok")).toBool() &&
        !outcome.value(QStringLiteral("mutationNotApplied")).toBool())
      ++successful_native_mutation_count;
  }
  const bool summarize_completed_native_rounds =
      successful_native_mutation_count >= 3 && latest_composition >= 0 &&
      !latest_current_composition_assessment.isEmpty();
  QVariantList mutation_receipts;
  for (const auto& item : results) {
    const auto value = item.toMap();
    const auto name = value.value(QStringLiteral("name")).toString();
    const auto arguments = value.value(QStringLiteral("arguments")).toMap();
    const auto operation = arguments.value(QStringLiteral("operation")).toString();
    if (name != QStringLiteral("photo.edit_composition") &&
        name != QStringLiteral("app.invoke_command")) continue;
    if (name == QStringLiteral("photo.edit_composition") &&
        operation == QStringLiteral("inspect")) continue;

    const auto result = value.value(QStringLiteral("result")).toMap();
    const auto content = result.value(QStringLiteral("structuredContent")).toMap();
    const auto argument_bytes = QJsonDocument::fromVariant(arguments).toJson(QJsonDocument::Compact);
    QVariantMap receipt{
        {QStringLiteral("toolName"), name},
        {QStringLiteral("argumentDigestSha256"), QString::fromLatin1(
             QCryptographicHash::hash(argument_bytes, QCryptographicHash::Sha256).toHex())},
        {QStringLiteral("ok"), result.value(QStringLiteral("ok"))},
    };
    // Preserve the exact accepted call (including requested copy and native
    // units) when a multi-round history is reduced to its receipt summary.
    if (!arguments.isEmpty())
      receipt.insert(QStringLiteral("arguments"), compact_prompt_evidence(arguments));
    for (const auto& key : {QStringLiteral("callId"), QStringLiteral("toolCallId"),
             QStringLiteral("originalCallId"), QStringLiteral("id")}) {
      const auto call_id = result.value(key, value.value(key)).toString();
      if (!call_id.isEmpty()) receipt.insert(QStringLiteral("originalCallId"), call_id.left(200));
    }
    for (const auto& key : {QStringLiteral("operation"), QStringLiteral("assetId"),
             QStringLiteral("layerId"), QStringLiteral("commandId")}) {
      const auto identity = arguments.value(key);
      if (identity.isValid()) receipt.insert(key, identity.toString().left(200));
    }
    const auto error = result.value(QStringLiteral("error"),
                                    result.value(QStringLiteral("toolExecutionError"))).toString();
    if (!error.isEmpty()) receipt.insert(QStringLiteral("error"), error.left(600));
    QVariantMap outcome;
    for (const auto& key : {QStringLiteral("ok"), QStringLiteral("changed"),
             QStringLiteral("mutationNotApplied"), QStringLiteral("recoverableError"),
             QStringLiteral("replayPrevented"), QStringLiteral("alreadyCompleted"),
             QStringLiteral("alreadySatisfied"), QStringLiteral("recoveryRequired"),
             QStringLiteral("errorCode")}) {
      if (content.contains(key)) outcome.insert(key, content.value(key));
    }
    for (const auto& key : {QStringLiteral("createdLayerId"), QStringLiteral("createdMaskId"),
             QStringLiteral("effectId"), QStringLiteral("createdClipId")}) {
      if (content.contains(key)) outcome.insert(key, content.value(key).toString().left(200));
    }
    for (const auto& key : {QStringLiteral("createdLayerIds"), QStringLiteral("createdClipIds")}) {
      const auto id_values = content.value(key).toList();
      QStringList compact_ids;
      for (qsizetype i = 0; i < std::min<qsizetype>(id_values.size(), 8); ++i)
        compact_ids.append(id_values.at(i).toString().left(200));
      if (!compact_ids.isEmpty()) {
        outcome.insert(key, compact_ids);
        if (id_values.size() > compact_ids.size())
          outcome.insert(key + QStringLiteral("OmittedCount"), id_values.size() - compact_ids.size());
      }
    }
    if (content.value(QStringLiteral("priorReturnedIds")).typeId() == QMetaType::QVariantMap) {
      QVariantMap returned_ids;
      const auto prior_ids = content.value(QStringLiteral("priorReturnedIds")).toMap();
      for (auto id = prior_ids.cbegin(); id != prior_ids.cend(); ++id) {
        if (id.key().endsWith(QStringLiteral("Id")) || id.key().endsWith(QStringLiteral("Ids")))
          returned_ids.insert(id.key(), compact_prompt_evidence(id.value(), id.key()));
      }
      if (!returned_ids.isEmpty()) outcome.insert(QStringLiteral("priorReturnedIds"), returned_ids);
    }
    if (!outcome.isEmpty()) receipt.insert(QStringLiteral("outcome"), outcome);
    mutation_receipts.append(receipt);
  }

  QVariantList proof_receipts;
  qsizetype proof_receipt_bytes = 0;
  constexpr qsizetype kMaximumProofReceiptHistoryBytes = 9000;
  for (const auto& value : values) {
    const auto receipt_value = value.toMap();
    if (!is_proof_or_validation_receipt(receipt_value)) continue;
    const auto receipt = compact_proof_receipt(receipt_value);
    const auto receipt_bytes = QJsonDocument::fromVariant(receipt)
                                  .toJson(QJsonDocument::Compact).size();
    if (receipt_bytes <= 0 || receipt_bytes > 5000) continue;
    while (!proof_receipts.isEmpty() && proof_receipt_bytes + receipt_bytes >
           kMaximumProofReceiptHistoryBytes) {
      proof_receipt_bytes -= QJsonDocument::fromVariant(proof_receipts.first())
                                 .toJson(QJsonDocument::Compact).size();
      proof_receipts.removeFirst();
    }
    proof_receipts.append(receipt);
    proof_receipt_bytes += receipt_bytes;
  }

  QVariantList compact_history = results;
  if (!latest_current_composition_assessment.isEmpty())
    compact_history.append(latest_current_composition_assessment);
  const auto compact_history_bytes = QJsonDocument::fromVariant(
      compact_prompt_evidence(compact_history)).toJson(QJsonDocument::Compact);
  if (results.size() <= 24 && compact_history_bytes.size() <= kMaximumPriorToolHistoryBytes &&
      !summarize_completed_native_rounds)
    return QString::fromUtf8(compact_history_bytes);
  if (results.size() <= 24) {
    QVariantMap receipt_summary{
        {QStringLiteral("contract"), QStringLiteral("agent-tool-history-with-protected-mutation-receipts-v1")},
        {QStringLiteral("completedToolCount"), results.size()},
      {QStringLiteral("mutationReceipts"), mutation_receipts},
      {QStringLiteral("toolCallValidationRecoveries"), validation_recoveries},
      {QStringLiteral("largePriorToolDetailsOmitted"), true},
      {QStringLiteral("inspectCurrentDocumentBeforePlanning"), latest_composition >= 0},
    };
    if (latest_composition >= 0)
      receipt_summary.insert(QStringLiteral("currentComposition"), results.at(latest_composition));
    else if (!latest_native_font_faces.isEmpty()) {
      qsizetype omitted_families = 0;
      qsizetype omitted_faces = 0;
      receipt_summary.insert(QStringLiteral("nativeFontFaces"), compact_font_face_inventory(
          latest_native_font_faces, &omitted_families, &omitted_faces));
    }
    if (!latest_font_probe.isEmpty())
      receipt_summary.insert(QStringLiteral("latestFontProbe"), latest_font_probe);
    if (!latest_current_composition_assessment.isEmpty())
      receipt_summary.insert(QStringLiteral("currentCompositionAssessment"),
          latest_current_composition_assessment);
    if (!latest_revision_baseline.isEmpty())
      receipt_summary.insert(QStringLiteral("revisionBaseline"), latest_revision_baseline);
    if (!proof_receipts.isEmpty()) receipt_summary.insert(QStringLiteral("proofReceipts"), proof_receipts);
    if (!approved_task_snapshots.isEmpty())
      receipt_summary.insert(QStringLiteral("approvedTaskSnapshots"), approved_task_snapshots);
    auto receipt_bytes = QJsonDocument::fromVariant(
        compact_prompt_evidence(receipt_summary)).toJson(QJsonDocument::Compact);
    if (receipt_bytes.size() > kMaximumPriorToolHistoryBytes && latest_composition >= 0) {
      receipt_summary.insert(QStringLiteral("currentComposition"), compact_current_composition_details(
          receipt_summary.value(QStringLiteral("currentComposition")).toMap()));
      receipt_summary.insert(QStringLiteral("currentStateDetailsCompacted"), true);
      receipt_bytes = QJsonDocument::fromVariant(
          compact_prompt_evidence(receipt_summary)).toJson(QJsonDocument::Compact);
    }
    if (receipt_bytes.size() > kMaximumPriorToolHistoryBytes) {
      receipt_summary.remove(QStringLiteral("latestFontProbe"));
      auto compact_mutations = receipt_summary.value(QStringLiteral("mutationReceipts")).toList();
      for (auto& mutation_value : compact_mutations) {
        auto mutation = mutation_value.toMap();
        mutation.remove(QStringLiteral("argumentDigestSha256"));
        mutation_value = mutation;
      }
      receipt_summary.insert(QStringLiteral("mutationReceipts"), compact_mutations);
      receipt_bytes = QJsonDocument::fromVariant(
          compact_prompt_evidence(receipt_summary)).toJson(QJsonDocument::Compact);
    }
    return QString::fromUtf8(receipt_bytes);
  }
  // A long task needs its latest document state even after the generic list
  // serializer's 64-entry limit. Keep actual created IDs separately from recent
  // receipts; full operation history remains in the run audit/task journal.
  QVariantMap created_resources;
  for (const auto& item : results) {
    const auto value = item.toMap();
    const auto content = value.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("structuredContent")).toMap();
    for (const auto& key : {QStringLiteral("createdLayerId"), QStringLiteral("createdMaskId"), QStringLiteral("effectId"), QStringLiteral("createdClipId")}) {
      const auto id = content.value(key).toString();
      if (!id.isEmpty()) created_resources.insert(id, key);
    }
    for (const auto& id : content.value(QStringLiteral("createdLayerIds")).toStringList())
      if (!id.isEmpty()) created_resources.insert(id, QStringLiteral("createdLayerId"));
    for (const auto& id : content.value(QStringLiteral("createdClipIds")).toStringList())
      if (!id.isEmpty()) created_resources.insert(id, QStringLiteral("createdClipId"));
  }
  auto summarized_history = QVariantMap{
      {QStringLiteral("contract"), QStringLiteral("agent-task-tool-context-v1")},
      {QStringLiteral("completedToolCount"), results.size()},
      {QStringLiteral("createdResources"), created_resources},
      {QStringLiteral("mutationReceipts"), mutation_receipts},
      {QStringLiteral("toolCallValidationRecoveries"), validation_recoveries},
      {QStringLiteral("currentComposition"), latest_composition >= 0 ? results.at(latest_composition) : QVariant{}},
      {QStringLiteral("earlierOperationCount"), std::max<qsizetype>(0, results.size() - 8)},
  };
  QVariantList recent_non_composition_results;
  for (qsizetype index = std::max<qsizetype>(0, results.size() - 16);
       index < results.size(); ++index) {
    const auto recent = results.at(index).toMap();
    const auto recent_name = recent.value(QStringLiteral("name")).toString();
    if (recent_name == QStringLiteral("photo.inspect_composition") ||
        recent_name == QStringLiteral("photo.edit_composition") ||
        recent_name == QStringLiteral("app.invoke_command"))
      continue;
    recent_non_composition_results.append(recent);
    while (recent_non_composition_results.size() > 8) recent_non_composition_results.removeFirst();
  }
  if (!recent_non_composition_results.isEmpty())
    summarized_history.insert(QStringLiteral("recentToolResults"), recent_non_composition_results);
  if (!proof_receipts.isEmpty()) summarized_history.insert(QStringLiteral("proofReceipts"), proof_receipts);
  if (!latest_font_probe.isEmpty()) summarized_history.insert(QStringLiteral("latestFontProbe"), latest_font_probe);
  if (!latest_current_composition_assessment.isEmpty())
    summarized_history.insert(QStringLiteral("currentCompositionAssessment"),
        latest_current_composition_assessment);
  if (!latest_revision_baseline.isEmpty())
    summarized_history.insert(QStringLiteral("revisionBaseline"), latest_revision_baseline);
  if (latest_composition < 0 && !latest_native_font_faces.isEmpty()) {
    qsizetype omitted_families = 0;
    qsizetype omitted_faces = 0;
    summarized_history.insert(QStringLiteral("nativeFontFaces"), compact_font_face_inventory(
        latest_native_font_faces, &omitted_families, &omitted_faces));
  }
  if (!approved_task_snapshots.isEmpty())
    summarized_history.insert(QStringLiteral("approvedTaskSnapshots"), approved_task_snapshots);
  auto summarized_bytes = QJsonDocument::fromVariant(
      compact_prompt_evidence(summarized_history)).toJson(QJsonDocument::Compact);
  if (summarized_bytes.size() > kMaximumPriorToolHistoryBytes) {
    summarized_history.remove(QStringLiteral("recentToolResults"));
    summarized_bytes = QJsonDocument::fromVariant(
        compact_prompt_evidence(summarized_history)).toJson(QJsonDocument::Compact);
  }
  if (summarized_bytes.size() > kMaximumPriorToolHistoryBytes && latest_composition >= 0) {
    summarized_history.insert(QStringLiteral("currentComposition"), compact_current_composition_details(
        summarized_history.value(QStringLiteral("currentComposition")).toMap()));
    summarized_history.insert(QStringLiteral("currentStateDetailsCompacted"), true);
    summarized_bytes = QJsonDocument::fromVariant(
        compact_prompt_evidence(summarized_history)).toJson(QJsonDocument::Compact);
  }
  if (summarized_bytes.size() > kMaximumPriorToolHistoryBytes) {
    summarized_history.remove(QStringLiteral("createdResources"));
    summarized_history.remove(QStringLiteral("latestFontProbe"));
    if (latest_composition >= 0) summarized_history.remove(QStringLiteral("nativeFontFaces"));
    auto compact_mutations = summarized_history.value(QStringLiteral("mutationReceipts")).toList();
    for (auto& mutation_value : compact_mutations) {
      auto mutation = mutation_value.toMap();
      mutation.remove(QStringLiteral("argumentDigestSha256"));
      mutation_value = mutation;
    }
    summarized_history.insert(QStringLiteral("mutationReceipts"), compact_mutations);
    summarized_history.insert(QStringLiteral("largePriorToolDetailsOmitted"), true);
    summarized_history.insert(QStringLiteral("inspectCurrentDocumentBeforePlanning"), latest_composition >= 0);
    summarized_bytes = QJsonDocument::fromVariant(
        compact_prompt_evidence(summarized_history)).toJson(QJsonDocument::Compact);
  }
  return QString::fromUtf8(summarized_bytes);
}

bool has_verified_unchanged_photo_baseline(
    const QVariantMap& validation_report,
    const QVariantMap& asset) {
  const auto evidence = validation_report.value(QStringLiteral("photographicQualityEvidence")).toMap();
  const auto valid_sha256 = [](const QVariant& value) {
    const auto hash = value.toString();
    return hash.size() == 64 && QRegularExpression(QStringLiteral("^[0-9a-fA-F]{64}$"))
        .match(hash).hasMatch();
  };
  const auto has_list = [&evidence](const QString& key) {
    const auto value = evidence.value(key);
    return value.typeId() == QMetaType::QVariantList ||
        value.typeId() == QMetaType::QStringList;
  };
  const auto has_nonnegative_number = [&evidence](const QString& key) {
    bool ok = false;
    const auto value = evidence.value(key).toDouble(&ok);
    return ok && std::isfinite(value) && value >= 0.0;
  };
  const auto report_asset_id = validation_report.value(QStringLiteral("assetId")).toString().trimmed();
  const auto evidence_asset_id = evidence.value(QStringLiteral("assetId")).toString().trimmed();
  const auto request_asset_id = asset.value(QStringLiteral("assetId")).toString().trimmed();
  const auto expected_asset_id = !request_asset_id.isEmpty() ? request_asset_id : report_asset_id;
  return evidence.value(QStringLiteral("contract")).toString() ==
             QStringLiteral("unchanged-pre-text-composition-photographic-baseline-v1") &&
      evidence.value(QStringLiteral("basis")).toString() ==
             QStringLiteral("unchanged-pre-text-composition") &&
      !evidence_asset_id.isEmpty() && !expected_asset_id.isEmpty() &&
      evidence_asset_id == expected_asset_id &&
      (report_asset_id.isEmpty() || report_asset_id == evidence_asset_id) &&
      valid_sha256(evidence.value(QStringLiteral("baselineSourceRecipeSha256"))) &&
      valid_sha256(evidence.value(QStringLiteral("baselineProofSha256"))) &&
      valid_sha256(evidence.value(QStringLiteral("baselineTaskStateHash"))) &&
      valid_sha256(evidence.value(QStringLiteral("normalizedPhotoCompositionSha256"))) &&
      evidence.value(QStringLiteral("photoRecipeUnchanged")).toBool() &&
      evidence.value(QStringLiteral("compositePhotoMetricsChangedOnlyByApprovedText")).toBool() &&
      !evidence.value(QStringLiteral("explicitPhotoQualityScope")).toBool() &&
      evidence.value(QStringLiteral("baselineImageStats")).typeId() == QMetaType::QVariantMap &&
      evidence.value(QStringLiteral("currentCompositeImageStats")).typeId() == QMetaType::QVariantMap &&
      evidence.value(QStringLiteral("baselineQualityScoreReport")).typeId() == QMetaType::QVariantMap &&
      has_nonnegative_number(QStringLiteral("baselineHighlightClipPercent")) &&
      has_nonnegative_number(QStringLiteral("compositeHighlightClipPercent")) &&
      has_nonnegative_number(QStringLiteral("highlightClipLimitPercent")) &&
      has_list(QStringLiteral("baselineWarnings")) &&
      has_list(QStringLiteral("currentCompositeWarnings")) &&
      has_list(QStringLiteral("introducedCompositeWarnings"));
}

QString approved_task_scope_instruction(const QVariantList& prior_tool_results) {
  const auto latest_approved_snapshot = [&prior_tool_results]() {
    QVariantMap latest;
    for (const auto& raw_value : prior_tool_results) {
      const auto value = raw_value.toMap();
      if (is_approved_task_snapshot_envelope(value))
        latest = value.value(QStringLiteral("snapshot")).toMap();
    }
    return latest;
  }();
  if (latest_approved_snapshot.isEmpty()) return {};
  return QStringLiteral(
      "The prior tool results contain the latest frozen approved-task snapshot. Treat only that snapshot's approvedInstructions, exactCopy, deliverables, targetIds, and taskScopedCapabilities as the user-approved task scope; do not union an earlier revision. Complete the already approved deliverables without asking for a second approval of the same scope during planning, and do not put an already approved construction operation in editIntent.requiresApproval when a matching supplied tool can do it. This does not waive the app's separate draft review or commit confirmation. The snapshot does not authorize extra deliverables, changed targets, or operations outside its taskScopedCapabilities, and it never overrides the active style, supplied tool schemas, runtime permissions, or safety rules. If a genuinely required choice is not covered and cannot be selected from returned native metadata, ask only about that unresolved choice. ");
}

QString current_assessment_revision_instruction(
    const AgentLocalModelPlanRequest& request) {
  const auto asset_id = request.asset.value(QStringLiteral("assetId")).toString().trimmed();
  if (asset_id.isEmpty()) return {};
  QVariantMap latest_revision_baseline;
  QVariantMap latest_current_assessment;
  for (const auto& raw_value : request.prior_tool_results) {
    const auto value = raw_value.toMap();
    const auto kind = value.value(QStringLiteral("kind")).toString();
    if (kind == QStringLiteral("draft-revision")) {
      const auto context = value.value(QStringLiteral("context")).toMap();
      const auto baseline = context.value(QStringLiteral("revisionCompositionBaseline")).toMap();
      if (baseline.value(QStringLiteral("assetId")).toString() == asset_id)
        latest_revision_baseline = baseline;
    } else if (kind == QStringLiteral("current-composition-assessment") &&
        value.value(QStringLiteral("contract")).toString() ==
            QStringLiteral("agent-current-composition-assessment-v1")) {
      if (value.value(QStringLiteral("assetId")).toString() != asset_id) continue;
      latest_current_assessment = value;
      const auto baseline = value.value(QStringLiteral("revisionBaseline")).toMap();
      if (baseline.value(QStringLiteral("assetId")).toString() == asset_id)
        latest_revision_baseline = baseline;
    }
  }
  QString instruction;
  if (!latest_current_assessment.isEmpty() &&
      latest_current_assessment.value(QStringLiteral("assetId")).toString() == asset_id) {
    instruction += QStringLiteral(
        "For this asset, use the newest current-composition-assessment and its preceding native inspection as the current defect and completion evidence. Treat earlier current-proof-validation as historical baseline only, not current status; use the latest assessment's requireProductivePlan and instruction with the current readback to choose the next action. This evidence-recency rule does not change approved scope or tool permissions. ");
  }
  if (!latest_revision_baseline.isEmpty() &&
      latest_revision_baseline.value(QStringLiteral("assetId")).toString() == asset_id) {
    instruction += QStringLiteral(
        "A matching revisionBaseline from the prior accepted native inspection is the pre-revision reference. Use it for relative typography corrections and apply each requested relative correction once; do not compound it on a size already changed in the current revision. Preserve the baseline's stable text-layer IDs, ordered copy, positions and transforms unless the approved scope explicitly asks to change them. Verify the result against the latest fresh native readback and rendered review. ");
  }
  return instruction;
}

bool explicit_font_face_pair_required(const AgentLocalModelPlanRequest& request) {
  QString scope = request.user_instructions;
  QString latest_approved_instructions;
  for (const auto& raw_value : request.prior_tool_results) {
    const auto value = raw_value.toMap();
    if (!is_approved_task_snapshot_envelope(value)) continue;
    latest_approved_instructions = value.value(QStringLiteral("snapshot")).toMap()
        .value(QStringLiteral("approvedInstructions")).toString();
  }
  if (!latest_approved_instructions.isEmpty())
    scope += QLatin1Char(' ') + latest_approved_instructions;
  const auto normalized_scope = scope.simplified().toLower();
  const bool names_family_and_face_pair =
      normalized_scope.contains(QStringLiteral("family and face")) ||
      normalized_scope.contains(QStringLiteral("face and family")) ||
      normalized_scope.contains(QStringLiteral("family, face")) ||
      normalized_scope.contains(QStringLiteral("face, family"));
  if (!names_family_and_face_pair)
    return false;
  return normalized_scope.contains(QStringLiteral("explicit")) ||
      normalized_scope.contains(QStringLiteral("must")) ||
      normalized_scope.contains(QStringLiteral("required")) ||
      normalized_scope.contains(QStringLiteral("set both")) ||
      normalized_scope.contains(QStringLiteral("choose a real font family and face"));
}

QStringList composition_context_validation_errors(
    const QJsonObject& arguments, const QJsonObject& input_schema,
    const CompositionGenerationContext& context, bool require_font_face_pair,
    QVariantMap* context_evidence) {
  QStringList errors;
  if (arguments.value(QStringLiteral("assetId")).toString().trimmed() != context.asset_id)
    return errors;

  const auto operation = arguments.value(QStringLiteral("operation")).toString();
  const auto layer_target = composition_operation_requires_layer_id(input_schema, operation);
  const auto layer_id = arguments.value(QStringLiteral("layerId")).toString().trimmed();
  if (context.layer_grounding_context_present && layer_target && !layer_id.isEmpty() &&
      !context.grounded_layer_ids.contains(layer_id)) {
    errors.append(QStringLiteral(
        "arguments.layerId '%1' is not grounded by the latest successful matching-asset inspection or a later successful creation receipt for asset '%2'")
        .arg(layer_id, context.asset_id));
  }

  if (operation == QStringLiteral("typography") && require_font_face_pair) {
    const auto values = arguments.value(QStringLiteral("values")).toObject();
    const auto family = values.value(QStringLiteral("family")).toString().trimmed();
    const auto face = values.value(QStringLiteral("face")).toString().trimmed();
    if (family.isEmpty())
      errors.append(QStringLiteral("arguments.values.family is required by the active brief"));
    if (face.isEmpty())
      errors.append(QStringLiteral("arguments.values.face is required by the active brief"));
    if (context.native_font_inventory_present) {
      const auto available_faces = native_font_faces_for_family(context.native_font_faces, family);
      if (!family.isEmpty() && !face.isEmpty() && !available_faces.contains(face)) {
        errors.append(QStringLiteral(
            "arguments.values family/face pair '%1' / '%2' is not present together in the retained native fontFaces inventory")
            .arg(family, face));
        if (context_evidence != nullptr) {
          context_evidence->insert(QStringLiteral("requestedFontFamily"), family);
          context_evidence->insert(QStringLiteral("requestedFontFace"), face);
          context_evidence->insert(QStringLiteral("availableFacesForRequestedFamily"), available_faces);
        }
      }
    } else if (context.authoritative_inspection_present && !family.isEmpty() && !face.isEmpty()) {
      errors.append(QStringLiteral(
          "arguments.values family/face pair cannot be verified because the matching asset has no retained native fontFaces inventory"));
    }
  }

  if (context_evidence != nullptr &&
      (context.layer_grounding_context_present || context.native_font_inventory_present)) {
    context_evidence->insert(QStringLiteral("assetId"), context.asset_id);
    context_evidence->insert(QStringLiteral("authoritativeInspectionPresent"),
        context.authoritative_inspection_present);
    context_evidence->insert(QStringLiteral("layerGroundingContextPresent"),
        context.layer_grounding_context_present);
    context_evidence->insert(QStringLiteral("groundedLayerIdCount"), context.grounded_layer_ids.size());
    context_evidence->insert(QStringLiteral("nativeFontInventoryPresent"),
        context.native_font_inventory_present);
    if (layer_target && !layer_id.isEmpty()) {
      context_evidence->insert(QStringLiteral("requestedLayerId"), layer_id);
      context_evidence->insert(QStringLiteral("requestedLayerIdGrounded"),
          context.grounded_layer_ids.contains(layer_id));
    }
  }
  return errors;
}

QString font_discovery_instruction(const AgentLocalModelPlanRequest& request) {
  const auto inspection_available = std::any_of(
      request.available_tool_schemas.cbegin(), request.available_tool_schemas.cend(),
      [](const QVariant& value) {
        return value.toMap().value(QStringLiteral("name")).toString() ==
            QStringLiteral("photo.inspect_composition");
      });
  if (!inspection_available) return {};
  auto instruction = QStringLiteral(
      "For typography discovery, make only one initial no-values photo.inspect_composition inventory read per asset; if a successful inventory is already in prior results, reuse it. Pass exactly {assetId} on that inventory read. Use returned fonts and fontFaces entries shaped as {family, faces} as supported native choices. Do not use inspect values to probe alternative candidates. After a mutation, a fresh photo.inspect_composition readback for the changed layer IDs is mandatory before any completion decision; if it is absent, request that readback. This state readback is distinct from the one-time font inventory and does not count as rediscovering fonts. Compare the latest textTypography fields against every explicit brief requirement and use tool_call_plan to repair any mismatch. For numeric relationships, calculate the actual ratio from readback values and check the requested threshold exactly. A minimum hierarchy ratio does not fix absolute font sizes. If the current smaller roles force a headline that cannot fit, reduce all involved sizes proportionally in one tool plan, unless the brief protects those sizes. Do not alternate enlarging a clipped headline and shrinking only that headline. Preserve revision-protected roles and geometry. An empty fontAxes list can describe a valid static font. When the user delegates design choices, choose a returned supported family and face. If the brief explicitly requires a native family and face and no returned face is available, ask or stop safely rather than using a default or substitution; use a default face only when the face is optional. For every typography value required by the brief, include it in the actual tool arguments; a summary does not apply a value. Default text is neutral white. When a color is requested, including warm white, explicitly set the intended color on every relevant text layer; a family/size-only update does not recolor it. Serialize typography values in canonical schema order: color, face, family, then later properties. If the brief explicitly requires both family and face, include both exact strings from the same returned inventory entry in every relevant typography call, with face before family; do not rely on a native default.");
  if (explicit_font_face_pair_required(request))
    instruction += QStringLiteral(
        " This task's active instructions explicitly require a native font family and face, so generated typography operations must provide both fields.");
  return instruction;
}

bool is_scoped_post_mutation_text_composition_request(
    const AgentLocalModelPlanRequest& request) {
  if (!request.allow_tool_call_plan || !request.require_tool_call_plan) return false;
  const auto asset_id = request.asset.value(QStringLiteral("assetId")).toString().trimmed();
  if (asset_id.isEmpty() || request.user_instructions.trimmed().isEmpty()) return false;

  bool has_inspection_tool = false;
  bool has_text_composition_operations = false;
  for (const auto& raw_tool : request.available_tool_schemas) {
    const auto tool = raw_tool.toMap();
    const auto name = tool.value(QStringLiteral("name")).toString();
    has_inspection_tool = has_inspection_tool || name == QStringLiteral("photo.inspect_composition");
    if (name != QStringLiteral("photo.edit_composition")) continue;
    const auto input_schema = tool.value(QStringLiteral("inputSchema")).toMap();
    const auto operation_enum = input_schema.value(QStringLiteral("properties")).toMap()
        .value(QStringLiteral("operation")).toMap().value(QStringLiteral("enum"));
    QStringList operation_values = operation_enum.toStringList();
    if (operation_values.isEmpty()) {
      for (const auto& operation : operation_enum.toList())
        operation_values.append(operation.toString());
    }
    has_text_composition_operations = operation_values.contains(QStringLiteral("add_text")) &&
        operation_values.contains(QStringLiteral("typography")) &&
        operation_values.contains(QStringLiteral("transform"));
  }
  if (!has_inspection_tool || !has_text_composition_operations) return false;

  QVariantMap approved_snapshot;
  QVariantMap latest_current_assessment;
  for (const auto& raw_value : request.prior_tool_results) {
    const auto value = raw_value.toMap();
    if (is_approved_task_snapshot_envelope(value))
      approved_snapshot = value.value(QStringLiteral("snapshot")).toMap();
    if (value.value(QStringLiteral("kind")).toString() ==
            QStringLiteral("current-composition-assessment") &&
        value.value(QStringLiteral("contract")).toString() ==
            QStringLiteral("agent-current-composition-assessment-v1") &&
        value.value(QStringLiteral("assetId")).toString() == asset_id)
      latest_current_assessment = value;
  }
  if (approved_snapshot.isEmpty() || latest_current_assessment.isEmpty() ||
      latest_current_assessment.value(QStringLiteral("batchRound")).toInt() < 1 ||
      latest_current_assessment.value(QStringLiteral("sourceProofSha256")).toString().isEmpty() ||
      latest_current_assessment.value(QStringLiteral("sourceRecipeSha256")).toString().isEmpty() ||
      approved_snapshot.value(QStringLiteral("approvedInstructions")).toString().trimmed() !=
          request.user_instructions.trimmed())
    return false;

  QStringList capabilities = approved_snapshot.value(QStringLiteral("taskScopedCapabilities")).toStringList();
  if (capabilities.isEmpty()) {
    for (const auto& capability : approved_snapshot.value(QStringLiteral("taskScopedCapabilities")).toList())
      capabilities.append(capability.toString());
  }
  capabilities.removeDuplicates();
  capabilities.sort();
  if (capabilities != QStringList{QStringLiteral("graphics"), QStringLiteral("layers_luts")})
    return false;

  QStringList targets = approved_snapshot.value(QStringLiteral("targetIds")).toStringList();
  if (targets.isEmpty()) {
    for (const auto& target : approved_snapshot.value(QStringLiteral("targetIds")).toList())
      targets.append(target.toString());
  }
  if (targets.size() != 1 || !targets.contains(asset_id) ||
      approved_snapshot.value(QStringLiteral("exactCopy")).toList().isEmpty() ||
      approved_snapshot.value(QStringLiteral("deliverables")).toList().isEmpty())
    return false;

  bool explicitly_preserves_photo_and_framing = false;
  for (const auto& raw_constraint : approved_snapshot.value(QStringLiteral("plan")).toMap()
           .value(QStringLiteral("constraints")).toList()) {
    const auto constraint = raw_constraint.toString().toLower();
    if (constraint.contains(QStringLiteral("preserv")) &&
        (constraint.contains(QStringLiteral("photo")) ||
         constraint.contains(QStringLiteral("photograph"))) &&
        constraint.contains(QStringLiteral("framing"))) {
      explicitly_preserves_photo_and_framing = true;
      break;
    }
  }
  if (!explicitly_preserves_photo_and_framing) return false;

  const auto actions = approved_snapshot.value(QStringLiteral("plan")).toMap()
      .value(QStringLiteral("actions")).toList();
  if (actions.isEmpty()) return false;
  return std::all_of(actions.cbegin(), actions.cend(), [](const QVariant& raw_action) {
    return raw_action.toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("text");
  });
}

QString ollama_agent_prompt(const AgentLocalModelPlanRequest& request) {
  const auto style_json =
      prompt_json(request.style);
  const auto available_tools_json =
      QString::fromUtf8(QJsonDocument::fromVariant(
          compact_tool_schemas_for_prompt(request.available_tool_schemas)).toJson(QJsonDocument::Compact));
  const auto prior_tool_results_json =
      prior_tool_results_prompt_json(request.prior_tool_results);
  const auto memory_packet_json =
      prompt_json(request.memory_packet);
  const auto examples = request.few_shot_examples.isEmpty()
                            ? compact_few_shot_examples_from_memory(request.memory_packet)
                            : request.few_shot_examples;
  const auto few_shot_examples_json =
      prompt_json(examples);
  const auto strategy_phase = normalized_strategy_phase(request.strategy_phase);
  const auto strategy_prompt = strategy_prompt_for_phase(strategy_phase);
  const auto unresolved_composition_instruction =
      request.require_tool_call_plan
          ? QStringLiteral(
                "Required approved document work remains unresolved after a rejected tool call or a rendered review that did not pass. Read prior tool results to identify the actual reason; do not claim that a rendered review failed unless the receipts say so. Use the available document tools to complete the approved work before returning any numeric edit intent. Do not force a particular tool, invent a mutation, or guess coordinates, layer IDs, mask IDs, or other missing facts; inspect first, ask a question, or stop safely when needed. Only tool_call_plan, needs_user_input, and cannot_complete_safely are allowed decisions for this turn. ")
          : QString{};
  const auto final_intent_instruction =
      request.require_tool_call_plan
          ? QStringLiteral(
                "This forced repair turn must not return edit_intent, even if the next operation appears complete. Continue approved work, ask a needed question, or stop safely; the task workflow will clear this requirement after the rejected call is corrected and any required rendered review passes. ")
          : request.allow_tool_call_plan ? QStringLiteral(
                "For tool-enabled responses, editIntent.completionStatus is required: use in_progress while any requested work or verification remains, and complete only after all explicit requirements are met in fresh readback. Do not return a passive in_progress intent while a supplied tool can finish an unresolved composition requirement; continue with tool_call_plan. Convert explicit copy, typography fields, color, placement, and numeric relationships into separate checklist items. If the readback misses any item, repair it with the matching tool before returning edit_intent. If you already have enough context and every item is complete, return decision=edit_intent with a semantic editIntent. ")
          : QStringLiteral(
                "If you already have enough context, return decision=edit_intent with a semantic editIntent. ");
  const auto tool_follow_up_instruction =
      request.require_tool_call_plan
          ? QStringLiteral(
                "After tool results, continue with non-repeating tool_call_plan rounds while approved work remains unresolved. Do not return edit_intent in this forced repair turn. Never repeat a successful operation. ")
          : QStringLiteral(
                "After any successful mutating tool plan, use the subsequent fresh photo.inspect_composition result to check the changed layers before deciding that work is done; if no fresh readback is present, request it. This readback is separate from one-time font discovery. Continue with non-repeating tool_call_plan rounds while construction, inspection, revision, or any explicit checklist item remains unfinished; return edit_intent only when the latest readback confirms completion. Never repeat a successful operation. ");
  auto prompt_template = QStringLiteral(
      "You are a local photo-editing planning agent. Return JSON only. "
      "The available-tool argumentGuide preserves host descriptions and operation-specific required fields; the supplied response format is authoritative for all exact argument constraints, native IDs, and enum choices. "
      "Use the phase strategy exactly; do not collapse sample, revision, batch, and outlier behavior into one generic plan. "
      "Treat explicit user constraints as authoritative: never choose monochrome, crop, reframing, or a global change when the user explicitly asks to preserve color, framing, or unaffected regions. "
      "Treat approved-project-edit and approved-project-correction examples as preference evidence only for the active project; never carry them into another project or override the current request. "
      "When approvedProjectTasteCard.twoSpeedPreference.influencesPlanning is true, use its planningGuidance only as project-local qualitative evidence about restraint versus deeper refinement. The current request, visible photo, active style, and safety policy always win. Never select First Look or Deep Review automatically, never suppress either proved version, and never commit from this preference. When influencesPlanning is false, treat the counts as disclosure-only evidence and do not alter the plan from them. "
      "When projectNorthStar is available, treat its brief as an explicitly authored, project-local qualitative goal. Use it only when compatible with the current request, active style, and safety policy; the current request wins every conflict. Never infer numeric recipe changes, culling, retouch, or destructive authority from the North Star alone. "
      "When photographerCalibrationCard is available, use its planningGuidance as project-local blind-review evidence: prefer restraint around validationConcernKeys, prioritize its weakest photographic dimensions, and keep uncertain results in user review. It is qualitative guidance only; never invent or change numeric thresholds, never claim professional readiness, and never let it override the current request. "
      "Treat recentVisualReferents as user-confirmed spatial references, not style preferences. Reuse one only when the user explicitly says same, that, this, chosen, or previously selected and the referent belongs to the exact active asset; never transfer its region to another photo and never resolve a vague pronoun by guessing. When applicable, keep the requested edit localized to that remembered object. "
      "For rejected-project-edit examples, avoid repeating the exact rejected recipe direction but never invent a rejection reason. "
      "Do not edit files directly and do not invent tools. "
      "Use only the listed tools when you need more context. "
      "%12"
      "When the supplied tools include photo.inspect_composition or photo.edit_composition and the user asks to create or change text, typography, paint, masks, selections, cutouts, layer order, or Photo Fragments, these composition operations MUST be requested through decision=tool_call_plan with the matching typed MCP tool and arguments. Do not answer with edit_intent, a recipe patch, or tone-only adjustments in place of the requested composition operation. Read composition state with photo.inspect_composition arguments {assetId}; do not pass operation or empty values. Never use photo.edit_composition with operation inspect. Inspect first when an existing layer or mask id is not provided; use only ids returned by inspection. Composition maskId means the selection-set ID returned by masks[].id, never an individual child-mask ID from recipe JSON. If the request already provides the exact active asset and mask or layer ids, do not spend a tool call rediscovering them; include the requested dependent edits in order in the same tool-call plan when the inputs are known. If a requested composition tool is unavailable, return cannot_complete_safely and explain the limitation. "
      "When current-proof validation evidence is available in prior tool results, use imageStats.histogram and imageStats.vectorscope to decide tone, white balance, saturation, skin, mask, and layer strategy. "
      "%13"
      "Ground visualFindings in visible evidence from the supplied image, name the affected target, and use normalized center/radius coordinates. For an arbitrary visible item such as a jacket, sign, window, flower, or specific person, set target=object and copy the concise visual noun phrase into targetLabel. "
      "Use localizedActions only when a bounded local change can improve one target without harming another; include at most two, require confidence of at least 0.6, and leave the array empty when localization is uncertain. "
      "Set selectionMode=semantic only when the intended selection is the complete visible subject, sky, or background. Set selectionMode=guided for a clearly visible arbitrary object with target=object and a specific targetLabel. If the user's phrase plausibly matches two or three visible instances and you cannot identify the exact instance with high confidence, include selectionCandidates for each plausible instance with a concise distinct label, confidence, and evidence region; do not guess silently. Omit selectionCandidates when one instance is clearly intended. Use region for a soft spatial area rather than an object. Always provide the evidence region because the app uses it for review and as a fail-closed radial fallback. "
      "Set globalChangePolicy=preserve when the user asks to change only one region or keep the rest unchanged; otherwise use allow. Preserving framing or protecting highlights does not forbid an explicitly requested overall look, so use allow for that combination. "
      "For an explicit poster, flyer, or social-graphic request, preserve approved copy exactly and create editable text using available typed composition or native command tools; keep the photo and user layers intact, and do not simulate typography with image-tone adjustments. If essential factual copy is missing, ask rather than inventing names, dates, prices or claims. If no authorized text tool is available, state that limitation and do not claim to have created a graphic. "
      "For requested layer, text or photo-effect work, inspect the composition, use the matching typed composition schema or discover a native command and read app.command_schema; target existing layers by exact ids, preserve unrelated properties and verify the rendered draft. Only use photo.get_recipe_patch_schema when a supplied patch tool actually requires it. Do not invent success when the selected layer is unavailable. "
      "localizedActions.adjustments is the only place you may provide bounded numeric adjustment deltas; the app clamps them and compiles non-destructive masks. Treat semantic and guided selection as requests until the app reports that its local selection runtime generated and validated the mask. "
      "Use retouchActions only for an explicit user request to heal a small blemish or remove a small, clearly identified visible distraction. Return an empty array for general enhancement, inferred cleanup, ambiguous targets, large regions, or confidence below 0.85. Use kind=blemish with operation=heal, and kind=distraction or object with operation=remove. Never infer face or body retouching. Set globalChangePolicy=preserve for a retouch-only request. The app compiles retouchActions into append-only cleanup operations and always requires user approval before creating the draft. If the style disallows retouch, return cannot_complete_safely. "
      "For the global edit intent describe the photographic outcome only; never author recipe fields, recipePatch, or file mutations. "
      "Keep the response compact and omit optional editIntent fields that do not change the requested result. "
      "If you need more context and tool calls are allowed, return decision=tool_call_plan with toolCalls. "
      "If user intent is ambiguous, return decision=needs_user_input with userQuestions. "
      "If the edit cannot be completed safely under the rulebook, return decision=cannot_complete_safely with reason. "
      "%14"
      "When app.list_commands is supplied, discover additional native editing controls by domain edit and query (for example shape, fill, group, source, typography, effect or exposure). Read app.command_schema for an available command, then use app.invoke_command with the exact commandId and typed values. Use actual createdLayerId(s) and inspected layer IDs; never pass UI indices. Unavailable commands have explicit missing ownership adapters. A replayPrevented=true or alreadyCompleted=true receipt means that exact earlier mutation completed; its mutationNotApplied flag describes only the duplicate attempt, so never repeat it and continue from its returned IDs plus a fresh inspection. For other errors, when a tool reports recoverableError=true and mutationNotApplied=true, correct that call using its error and fresh inspection/schema. Completed earlier calls remain applied; never replay them. Calls after the rejected call were withheld and still need planning. Do not declare completion while the rejected step is unresolved. Creation followed by edits targeting its new ID requires another turn after the creation receipt; never predict IDs or use placeholders in the same batch. A successful matching-asset inspection replaces the earlier known layer-ID set; only a successful creation receipt after that inspection adds another usable ID. Every layer-targeted call must use one of those exact current IDs. "
      "For Timeline work, discover domain timeline and inspectAgentDocument, then read the available command schemas. Use actual returned createdClipId(s) and inspected clip IDs for dependent steps. Preserve the photo and Timeline as separate resources; a photo recipe alone does not represent a completed Timeline edit. Never guess clip IDs or use a UI clip index. "
      "Include styleConstraintCitations with active style fields when returning edit intent.\n"
      "Strategy phase: %1\n"
      "Phase strategy:\n%2\n"
      "User instructions:\n%3\n"
      "Current adjustment patch JSON:\n%4\n"
      "Style rulebook JSON:\n%5\n"
      "Retrieved memory packet JSON:\n%6\n"
      "Compact relevant examples JSON:\n%7\n"
      "Tool-call planning allowed: %8\n"
      "Available MCP tool schemas JSON:\n%9\n"
      "Current rendered planning evidence JSON:\n%11\n"
      "The first attached planning image represents the completed recipe state. Subsequent images, when present, are targeted detail crops listed in the same order in targetedDetailProofs; use them only as supplementary visual evidence. Earlier receipts describe prior states.\n"
      "Prior MCP tool results JSON:\n%10");
  if (is_scoped_post_mutation_text_composition_request(request)) {
    prompt_template = QStringLiteral(
        "Scoped post-mutation text-composition repair prompt v1. You are a local photo-editing planning agent; return JSON only and follow the supplied phase strategy and response schema. "
        "Use only the latest frozen approved task snapshot as scope. Preserve its exact copy, target asset, deliverables and capabilities. This repair covers editable text and layout; preserve the photograph, framing, color and unrelated layers. Do not substitute photo-tone changes for requested text or add unapproved content. "
        "Use supplied typed composition tools, their argument guide and current native readback. Use only returned stable IDs; never guess IDs or repeat successful mutations. Put every requested copy and typography value in actual tool arguments. "
        "Check exact copy, legibility at intended output size, contrast, typography, position, size, hierarchy, spacing, safe margins, subject/focal-area clearance and layer order against the latest native readback and rendered overview/detail evidence. Metadata and successful receipts alone do not prove visible correctness. For requested text behind a subject, retain intended foreground occlusion but distinguish it from edge clipping, missing copy or accidental collisions. "
        "Use project taste only when influencesPlanning is true and only as project-local qualitative guidance; when false, counts are disclosure only. Use the North Star as a qualitative goal, calibration as qualitative evidence without changing thresholds, and referents only for the same asset when explicitly invoked. The current brief, active style and safety policy win. Never infer numeric or destructive authority or transfer referents between assets. Continue tool_call_plan while approved work remains; this forced repair turn must not return edit_intent. Ask only for essential missing information or stop safely when evidence or capability is missing. "
        "%12%13%14\n"
        "Strategy phase: %1\n"
        "Phase strategy:\n%2\n"
        "User instructions:\n%3\n"
        "Current adjustment patch JSON:\n%4\n"
        "Style rulebook JSON:\n%5\n"
        "Retrieved memory packet JSON:\n%6\n"
        "Compact relevant examples JSON:\n%7\n"
        "Tool-call planning allowed: %8\n"
        "Available MCP tool schemas JSON:\n%9\n"
        "Current rendered planning evidence JSON:\n%11\n"
        "The first attached planning image is the current rendered overview; subsequent images are supplemental detail crops. Earlier receipts describe prior states.\n"
        "Prior MCP tool results JSON:\n%10");
  }
  return prompt_template.arg(
          strategy_phase,
          strategy_prompt,
          user_instructions_for_prompt(request),
          prompt_json(QJsonDocument::fromJson(
              current_adjustment_patch_for_prompt(request.current_adjustment_patch_json).toUtf8()).toVariant()),
          style_json,
          memory_packet_json,
      few_shot_examples_json,
      request.allow_tool_call_plan ? QStringLiteral("true") : QStringLiteral("false"),
      available_tools_json,
      prior_tool_results_json,
      prompt_json(request.asset.value(QStringLiteral("visualEvidence"))),
      unresolved_composition_instruction,
      final_intent_instruction,
      tool_follow_up_instruction);
}

qsizetype prompt_section_byte_count(
    const QString& prompt,
    const QString& section_label,
    const QString& next_section_label = {}) {
  const auto section_start = prompt.indexOf(section_label);
  if (section_start < 0) return 0;
  const auto value_start = section_start + section_label.size();
  auto value_end = next_section_label.isEmpty()
      ? prompt.size() : prompt.indexOf(next_section_label, value_start);
  if (value_end < value_start) value_end = prompt.size();
  return prompt.mid(value_start, value_end - value_start).trimmed().toUtf8().size();
}

QVariantList compact_proof_critique_previous_attempts(const QVariantList& attempts) {
  QVariantList summaries;
  summaries.reserve(attempts.size());
  for (const auto& attempt_value : attempts) {
    const auto attempt = attempt_value.toMap();
    const auto validation = attempt.value(QStringLiteral("validationReport")).toMap();
    const auto photo_evidence = validation
        .value(QStringLiteral("photographicQualityEvidence")).toMap();
    const auto composition_review = validation.value(QStringLiteral("compositionReview")).toMap();
    const auto detail_report = composition_review.value(QStringLiteral("detailProofReport")).toMap();
    const auto inspection = composition_review.value(QStringLiteral("inspection")).toMap();
    const auto model_critique = attempt.value(QStringLiteral("modelProofCritique")).toMap();
    auto report_sha256 = attempt.value(QStringLiteral("validationReportSha256"))
        .toString().toLatin1();
    static const QRegularExpression report_hash_pattern(QStringLiteral("^[0-9a-fA-F]{64}$"));
    if (!report_hash_pattern.match(QString::fromLatin1(report_sha256)).hasMatch()) {
      const auto report_bytes = QJsonDocument::fromVariant(validation)
          .toJson(QJsonDocument::Compact);
      report_sha256 = report_bytes.isEmpty() ? QByteArray{} :
          QCryptographicHash::hash(report_bytes, QCryptographicHash::Sha256).toHex();
    }
    // The canonical report digest binds source/task hashes; retain the small
    // identity tuple and proof hash alongside that full-report digest.
    QVariantMap binding{
        {QStringLiteral("assetId"), validation.value(QStringLiteral("assetId"))},
        {QStringLiteral("runId"), inspection.value(QStringLiteral("runId"))},
        {QStringLiteral("taskId"), photo_evidence.value(QStringLiteral("currentTaskId"))},
        {QStringLiteral("proofSha256"), photo_evidence.value(QStringLiteral("currentProofSha256"))},
    };
    if (binding.value(QStringLiteral("assetId")).toString().isEmpty())
      binding.insert(QStringLiteral("assetId"), photo_evidence.value(QStringLiteral("assetId")));
    if (binding.value(QStringLiteral("proofSha256")).toString().isEmpty())
      binding.insert(QStringLiteral("proofSha256"), detail_report.value(QStringLiteral("overviewSha256")));

    QVariantMap critique_summary;
    for (const auto& key : {QStringLiteral("modelId"), QStringLiteral("ok"),
             QStringLiteral("pass"), QStringLiteral("summary"), QStringLiteral("issues"),
             QStringLiteral("repairHints"), QStringLiteral("needsUserReview"),
             QStringLiteral("tasteConfidence"), QStringLiteral("warnings")}) {
      if (model_critique.contains(key))
        critique_summary.insert(key, model_critique.value(key));
    }
    summaries.append(QVariantMap{
        {QStringLiteral("contract"), QStringLiteral("proof-critique-history-summary-v1")},
        {QStringLiteral("attemptIndex"), attempt.value(QStringLiteral("attemptIndex"))},
        {QStringLiteral("patchHash"), attempt.value(QStringLiteral("patchHash"))},
        {QStringLiteral("repairSource"), attempt.value(QStringLiteral("repairSource"))},
        {QStringLiteral("qualityScore"), attempt.value(QStringLiteral("qualityScore"))},
        {QStringLiteral("warnings"), attempt.value(QStringLiteral("warnings"))},
        {QStringLiteral("needsUserReview"), attempt.value(QStringLiteral("needsUserReview"))},
        {QStringLiteral("stopReasonCandidate"), attempt.value(QStringLiteral("stopReasonCandidate"))},
        {QStringLiteral("validationOk"), validation.value(QStringLiteral("ok"))},
        {QStringLiteral("validationNeedsUserReview"), validation.value(QStringLiteral("needsUserReview"))},
        {QStringLiteral("modelProofCritique"), critique_summary},
        {QStringLiteral("historicalValidationBinding"), binding},
        {QStringLiteral("validationReportSha256"), QString::fromLatin1(report_sha256)},
        {QStringLiteral("fullValidationReportOmittedFromCriticContext"), true},
    });
  }
  return summaries;
}

QString ollama_proof_critique_prompt(const AgentLocalModelProofCritiqueRequest& request) {
  const auto scoped_unchanged_photo_graphic_review =
      !request.graphic_layers.isEmpty() &&
      has_verified_unchanged_photo_baseline(request.validation_report, request.asset);
  const auto style_json =
      prompt_json(request.style);
  const auto graphic_layers_json = prompt_json(
      compact_prompt_expected_graphic_rows(request.graphic_layers));
  const auto validation_json =
      prompt_json(proof_critique_validation_prompt_projection(
          request.validation_report, request.graphic_layers, request.user_instructions,
          request.asset));
  const auto attempts_json =
      prompt_json(compact_proof_critique_previous_attempts(request.previous_attempts));
  const auto calibration_json = prompt_json(request.photographer_calibration);
  const auto project_north_star_json = prompt_json(request.project_north_star);
  const auto generic_critique_instructions = QStringLiteral(
      "You are a local photo-editing proof evaluator. Return JSON only; do not propose recipePatch or mutate files. Binary mask/image payloads remain local and are omitted from metadata. Missing or omitted evidence requires human review. Judge rendered after pixels against user instructions, active style and deterministic validation. "
      "When expectedGraphicTextLayers is nonempty, compare every expected text string with the rendered after image. Check complete, unclipped copy, intended-size readability, contrast and hierarchy, safe margins, collisions, and subject/focal clearance. Requested text-behind-subject intentional foreground occlusion is part of the layout; distinguish it from clipping or accidental collisions. Missing, altered, clipped, colliding or illegible text fails with a specific issue and repair hint. Insufficient pixels to verify copy, layout or requested occlusion means needsUserReview. Correct metadata alone is not visible proof. "
      "Attached overview/detail images may be downsampled evidence; their dimensions are not the target canvas. Use validationReport.compositionReview.detailProofReport.renderedCompositionWidth/Height when available, then decoded observed-layer canvasWidth/Height. If size/aspect is unavailable, do not infer them from the thumbnail evidence; require review. "
      "Read validationReport.imageStats.histogram as a histogram and vectorscope as a vectorscope. Treat clipping, compressed tonal range, channel cast, hue imbalance, skin drift, unsafe retouch, broken masks or style mismatch as failure/repair conditions. Use repairHints for a later semantic-intent repair; never include raw image data or recipe JSON.\n"
      "Photographer calibration is qualitative only; if proof touches validationConcernKeys and taste is uncertain, require review. Never change numeric thresholds or claim calibration proves readiness.\n"
      "The Project North Star is qualitative only. The current request, active style constraints, deterministic validation, and safety policy win every conflict. Never fail solely because it lacks an unrequested numeric or destructive change.\n"
      "Previous attempts are compact historical summaries with validationReportSha256 bindings; use their prior issues, repair hints and scores as context only. The current rendered proof, native checks and current task binding determine this review.\n"
      "User instructions:\n%1\n"
      "Style rulebook JSON:\n%2\n"
      "Validation report JSON:\n%3\n"
      "Previous attempts JSON:\n%4\n"
      "Photographer calibration JSON:\n%5\n"
      "Project North Star JSON:\n%6\n"
      "Expected graphic text layers JSON:\n%7")
      ;
  const auto scoped_graphic_critique_instructions = QStringLiteral(
      "Read-only Looksy review (looksy-workmanship-v1); JSON only. Approved instructions, style, deterministic validation and safety govern; calibration/North Star cannot override them or change thresholds. Missing graphic/crop evidence or unknown canvas requires human review; metadata and edit success are not visual proof.\n"
      "Use bound prior issues, repair hints and scores as context only; judge current pixels, native checks and task binding.\n"
      "Compare every expected copy with overview/detail pixels for completeness, intended-size readability, contrast, clipping, safe margins, hierarchy, collisions and focal clearance. Requested foreground occlusion is intentional; distinguish it from accidental collision or clipping. Images may be downsampled: use renderedCompositionWidth/Height or expected-layer canvas size, never thumbnail size. layoutMetrics.renderedBounds/effectiveTypographySize bound transformed-frame fit and size ratios only; judge glyph ink, edges/effects and legibility from pixels. Give short semantic repairHints; never include raw images or recipe JSON.\n"
      "User instructions:\n%1\n"
      "Style rulebook JSON:\n%2\n"
      "Validation report JSON:\n%3\n"
      "Previous attempts JSON:\n%4\n"
      "Photographer calibration JSON:\n%5\n"
      "Project North Star JSON:\n%6\n"
      "Expected graphic text layers JSON:\n%7");
  auto prompt = (scoped_unchanged_photo_graphic_review
      ? scoped_graphic_critique_instructions
      : generic_critique_instructions)
      .arg(
          request.user_instructions,
          style_json,
          validation_json,
          attempts_json,
          calibration_json,
          project_north_star_json,
          graphic_layers_json);
  prompt.prepend(QStringLiteral(
      "Verify each explicit spatial instruction against the overview's named image region, not just native in-bounds geometry. For example, a title across mountain terrain does not satisfy placement in open sky even if readable. Wrong-region placement fails; suggest moving the existing layer into the requested visible region after checking its transform schema. Font-size changes alone cannot correct placement. Preserve protected photograph and framing.\n"
      "Type hierarchy arithmetic: use effectiveTypographySize; at least R means a ratio >= R, including equality. 144/48=3, 96/32=3 and 72/24=3 all satisfy at least 3; 80/24 is greater than 3. Do not invent a failed native ratio check when the current native values meet it. Typography tracking is absolute additional canvas pixels, not thousandths of an em; 40 adds 40 pixels between glyphs. A width repair must preserve the required ratio and readable smaller roles jointly; reduce excessive tracking before proposing incompatible size changes. Layer stack order causes occlusion only where visible pixels overlap; use current bounds and pixels instead of assuming every lower text layer is obscured.\n"));
  if (scoped_unchanged_photo_graphic_review) {
    prompt += QStringLiteral(
        "\nVerified unchanged-photo basis binds this asset to its pre-text baseline and unchanged photo recipe; only approved text pixels changed composite metrics, and no photo-quality scope was requested. Keep baseline warnings/measurements as existing observations; use baseline image statistics and thresholds for photo tone, exposure and clipping. Baseline warnings alone or text pixels must not trigger photo edits/failure. Judge glyph defects from full composite pixels and currentCompositeWarnings. Fail new photo deterioration only when bound evidence proves it; this never waives graphic or deterministic checks.");
    if (prompt.contains(QStringLiteral("photo-scalar-v1"))) {
      prompt += QStringLiteral(
          " Histogram/vectorscope bins are omitted only from this prompt; native validation retains them. Do not claim review of omitted bins.");
    }
  }
  prompt += QStringLiteral(
      "\nDecode lossless-columnar-rows-v1: commonFields seed rows; fields[i] maps to rows[*][i]; mask 1=present (incl null), 0=absent; rowIds align with identityField. rowIdsReference, eligibleLayerIdsReference and observedLayerOrderIdsTopFirstReference resolve in order to Expected graphic text layers JSON.rowIds. Resolve {$h1:n} through hashes[n]. Join typography/layout tables by id. observedGraphicLayersReference names the exact decoded expected rows; expectedGraphicTextLayerReference names the matching row, with exactFieldMatches listing equal fields. geometryChecks layoutMetricsReference uses geometryChecksLayoutMetricsReferenceFields. approvedBriefPlanReference resolves fields in approvedBriefPlanExactFields to Approved brief.plan; expectedDeliverablesReference resolves to Approved brief.deliverables. introducedCompositeWarningsReference and compositeWarningsReference resolve to photographicQualityEvidence.currentCompositeWarnings. distributionEvidenceProjection names omitted bins; do not claim to review unseen distributions. Image 0 is the overview; attachedImageIndex selects its exact crop. Asset paths change only on exact match; hashes, bounds, IDs and omission flags remain.");
  return prompt;
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
  const auto scoped_text_composition_prompt =
      is_scoped_post_mutation_text_composition_request(request);
  const auto prompt = ollama_agent_prompt(request);
  const auto strategy_phase = normalized_strategy_phase(request.strategy_phase);
  const auto strategy_prompt = strategy_prompt_for_phase(strategy_phase);
  const auto few_shot_examples = request.few_shot_examples.isEmpty()
                                     ? compact_few_shot_examples_from_memory(request.memory_packet)
                                     : request.few_shot_examples;
  const auto photographer_calibration =
      request.memory_packet
          .value(QStringLiteral("photographerCalibrationCard"))
          .toMap();
  const auto project_north_star =
      request.memory_packet.value(QStringLiteral("projectNorthStar")).toMap();
  const auto approved_scope_instruction =
      approved_task_scope_instruction(request.prior_tool_results);
  const auto typography_discovery_instruction =
      font_discovery_instruction(request);
  const auto revision_baseline_instruction =
      current_assessment_revision_instruction(request);
  const auto system_prompt =
      QStringLiteral("Return one compact JSON object matching the supplied schema. ") +
      agent_workmanship_guidance() + approved_scope_instruction + typography_discovery_instruction +
      revision_baseline_instruction +
      (request.allow_tool_call_plan
           ? QStringLiteral("When a supplied photo composition tool matches an explicit text, typography, paint, mask, selection, cutout, layer-order, or Photo Fragment request, return decision=tool_call_plan and invoke that tool; do not substitute edit_intent or photo-tone changes. Read composition state with photo.inspect_composition arguments {assetId}; do not pass operation or empty values. Never use photo.edit_composition with operation inspect. Inspect first if a required existing layer or mask id is unknown. Serialize photo.edit_composition arguments in the supplied schema order: assetId, layerId when required, operation, values. Keep each values object in the supplied property order and use the exact requested operation. If the request gives exact active asset and mask or layer ids, use them directly and include dependent edits in order in the same tool-call plan. After successful tool results, continue necessary tool rounds until requested editing work is complete; never repeat a successful operation. Discover missing native design controls with app.list_commands, read app.command_schema and invoke only available controls with app.invoke_command and stable layer IDs. A successful result with alreadySatisfied=true and changed=false confirms the requested state already exists; continue without retrying it to force a mutation. ")
           : QStringLiteral("Tool calls are disabled for this turn; you must not return decision=tool_call_plan. ")) +
      (request.require_tool_call_plan
           ? QStringLiteral("Approved document work remains unresolved after a rejected tool call or a rendered review that did not pass. Read prior receipts to identify the actual cause; do not claim a rendered review failed unless a receipt says so. Continue with available document tools before any numeric edit intent; do not return edit_intent. Do not guess tools, coordinates, layer IDs, mask IDs, or mutations. A valid user question or a safe cannot_complete_safely response remains allowed. ")
           : QString{}) +
      QStringLiteral("For an edit_intent response include summary, styleConstraintCitations, editIntent, and optional warnings. ") +
      QStringLiteral("When returning edit_intent after tool use, set editIntent.completionStatus to complete only when all approved requested work is finished and the tool receipts and required readback support that conclusion; use in_progress when any requested work or verification remains. Never use an empty preserve intent to represent unfinished work. ") +
      QStringLiteral("When supplied, the first image is the required rendered overview and subsequent images are targeted detail crops in the same order as visualEvidence.targetedDetailProofs; use them as supplementary proof only. ") +
      QStringLiteral("The app owns recipe compilation and application.");
  QJsonObject user_message{
      {QStringLiteral("role"), QStringLiteral("user")},
      {QStringLiteral("content"), prompt},
  };
  const auto image_payload = image_payload_for_asset(
      request.asset,
      ollama_planning_image_edge(model));
  const auto additional_images = additional_proof_images_for_asset(
      request.asset, ollama_planning_image_edge(model), false);
  if (!additional_images.valid) {
    result.error_message = additional_images.error_message;
    result.prompt_audit.insert(QStringLiteral("errorCode"), QStringLiteral("planning_proof_unavailable"));
    return result;
  }
  const auto additional_proof_requested = !additional_images.paths.isEmpty();
  const auto image_included = !image_payload.base64.isEmpty() && model.value(QStringLiteral("vision")).toBool();
  if (request.asset.value(QStringLiteral("planningProofRequired")).toBool() && !image_included) {
    result.error_message = QStringLiteral("The completed planning proof is missing, changed or unreadable, or the selected model cannot inspect images.");
    result.prompt_audit.insert(QStringLiteral("errorCode"), QStringLiteral("planning_proof_unavailable"));
    return result;
  }
  if (additional_proof_requested && !model.value(QStringLiteral("vision")).toBool()) {
    result.error_message = QStringLiteral("The selected local model cannot inspect the required targeted detail proof images.");
    result.prompt_audit.insert(QStringLiteral("errorCode"), QStringLiteral("planning_proof_unavailable"));
    return result;
  }
  const auto composition_context = composition_generation_context(request);
  const auto native_font_inventory_entry_count = composition_context.native_font_faces.size();
  const auto require_font_face_pair = explicit_font_face_pair_required(request);
  const auto generation_schema =
      ollama_generation_payload_schema(
          request.allow_tool_call_plan, request.available_tool_schemas,
          request.require_tool_call_plan,
          require_font_face_pair, &composition_context);
  const auto planning_max_output_tokens =
      ollama_planning_max_output_tokens(model);
  const auto attached_image_count = image_included
      ? 1 + additional_images.payloads.size() : 0;
  QList<QSize> attached_image_sizes;
  if (image_included) {
    attached_image_sizes.append(image_payload.pixel_size);
    for (const auto& detail : additional_images.payloads)
      attached_image_sizes.append(detail.pixel_size);
  }
  const auto system_prompt_utf8_bytes = static_cast<qint64>(system_prompt.toUtf8().size());
  const auto user_prompt_utf8_bytes = static_cast<qint64>(prompt.toUtf8().size());
  const auto aggregate_text_utf8_bytes = system_prompt_utf8_bytes + user_prompt_utf8_bytes;
  int estimated_aggregate_text_tokens = 0;
  qint64 estimated_ascii_utf8_bytes = 0;
  qint64 estimated_non_ascii_utf8_bytes = 0;
  qint64 estimated_non_ascii_code_points = 0;
  QVariantList image_context_reservations;
  const auto estimated_context_tokens_before_cap = ollama_planning_context_requirement(
      system_prompt, prompt, model_name, attached_image_sizes,
      static_cast<int>(attached_image_count), planning_max_output_tokens,
      &estimated_aggregate_text_tokens, &estimated_ascii_utf8_bytes,
      &estimated_non_ascii_utf8_bytes, &estimated_non_ascii_code_points,
      &image_context_reservations);
  const auto context_token_limit = std::min(ollama_context_tokens(model), 24576);
  const auto context_tokens = context_token_limit;
  const auto tool_catalog_bytes = prompt_section_byte_count(
      prompt,
      QStringLiteral("Available MCP tool schemas JSON:\n"),
      QStringLiteral("Current rendered planning evidence JSON:\n"));
  const auto prior_tool_history_bytes = prompt_section_byte_count(
      prompt, QStringLiteral("Prior MCP tool results JSON:\n"));
  qint64 additional_proof_image_bytes = 0;
  for (const auto& detail : additional_images.payloads)
    additional_proof_image_bytes += detail.transmitted_bytes;
  const QVariantMap prompt_component_audit{
      {QStringLiteral("contract"), QStringLiteral("ollama-planning-prompt-components-v1")},
      {QStringLiteral("systemPromptUtf8Bytes"), system_prompt_utf8_bytes},
      {QStringLiteral("userPromptUtf8Bytes"), user_prompt_utf8_bytes},
      {QStringLiteral("aggregateTextUtf8Bytes"), aggregate_text_utf8_bytes},
      {QStringLiteral("userGuidanceMode"), scoped_text_composition_prompt
           ? QStringLiteral("scoped_post_mutation_text_composition_v1")
           : QStringLiteral("full_planning_guidance_v1")},
      {QStringLiteral("availableToolCatalogUtf8Bytes"), tool_catalog_bytes},
      {QStringLiteral("priorToolHistoryUtf8Bytes"), prior_tool_history_bytes},
      {QStringLiteral("generationSchemaUtf8Bytes"),
       QJsonDocument(generation_schema).toJson(QJsonDocument::Compact).size()},
      {QStringLiteral("attachedImageCount"), attached_image_count},
      {QStringLiteral("imageContextReservations"), image_context_reservations},
      {QStringLiteral("primaryImageTransmittedBytes"), image_included
           ? image_payload.transmitted_bytes : 0},
      {QStringLiteral("additionalProofImageCount"), image_included
           ? additional_images.payloads.size() : 0},
      {QStringLiteral("additionalProofImageTransmittedBytes"), image_included
           ? additional_proof_image_bytes : 0},
      {QStringLiteral("contextTokensRequested"), context_tokens},
      {QStringLiteral("contextTokenLimit"), context_token_limit},
      {QStringLiteral("estimatedAggregateTextTokens"), estimated_aggregate_text_tokens},
      {QStringLiteral("estimatedAsciiUtf8Bytes"), estimated_ascii_utf8_bytes},
      {QStringLiteral("estimatedNonAsciiUtf8Bytes"), estimated_non_ascii_utf8_bytes},
      {QStringLiteral("estimatedNonAsciiCodePointCount"), estimated_non_ascii_code_points},
      {QStringLiteral("estimatedContextTokensBeforeCap"),
       estimated_context_tokens_before_cap},
      {QStringLiteral("contextEstimateMethod"),
       QStringLiteral("ceil(ascii_utf8_bytes / 4) + non_ascii_utf8_bytes + 512 + output_tokens + sum(imageContextReservations); Qwen3.5/Qwen3.6 valid transmitted dimensions <=1600px per edge use max(2048, ceil512(ceil(w/32)*ceil(h/32)+512)); other recognized Qwen vision images <=768px per edge reserve 2048; all other images reserve 4096; conservative processor/dimension proxy, not exact Ollama tokenization; non-ASCII is charged by UTF-8 byte fallback; format grammar audited separately")},
  };
  if (image_included) {
    QJsonArray images{image_payload.base64};
    for (const auto& detail : additional_images.payloads) images.append(detail.base64);
    user_message.insert(QStringLiteral("images"), images);
  }
  const QJsonObject payload{
      {QStringLiteral("model"), model_name},
      {QStringLiteral("stream"), false},
      {QStringLiteral("think"), false},
      {QStringLiteral("keep_alive"), QStringLiteral("10m")},
      {QStringLiteral("format"), generation_schema},
      {QStringLiteral("messages"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("role"), QStringLiteral("system")},
               {QStringLiteral("content"), system_prompt},
           },
           user_message,
       }},
      {QStringLiteral("options"),
       QJsonObject{
           {QStringLiteral("temperature"), 0.0},
           {QStringLiteral("num_predict"), planning_max_output_tokens},
           {QStringLiteral("num_ctx"), context_tokens},
       }},
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
      {QStringLiteral("contextTokens"), context_tokens},
      {QStringLiteral("promptComponents"), prompt_component_audit},
      {QStringLiteral("systemPrompt"), system_prompt},
      {QStringLiteral("userPrompt"), prompt},
      {QStringLiteral("userPromptCharCount"), prompt.size()},
      {QStringLiteral("availableToolCount"), request.available_tool_schemas.size()},
      {QStringLiteral("priorToolResultCount"), request.prior_tool_results.size()},
      {QStringLiteral("toolCallPlanningAllowed"), request.allow_tool_call_plan},
      {QStringLiteral("toolCallPlanRequired"), request.require_tool_call_plan},
      {QStringLiteral("memoryPacketPhase"), request.memory_packet.value(QStringLiteral("phase"))},
      {QStringLiteral("includedMemoryKinds"),
       request.memory_packet.value(QStringLiteral("includedMemoryKinds"))},
      {QStringLiteral("rawMemoryVaultIncluded"),
       request.memory_packet.value(QStringLiteral("rawVaultIncluded")).toBool()},
      {QStringLiteral("fewShotExampleCount"), few_shot_examples.size()},
      {QStringLiteral("photographerCalibrationAvailable"),
       photographer_calibration.value(QStringLiteral("available")).toBool()},
      {QStringLiteral("photographerCalibrationDigest"),
       photographer_calibration.value(QStringLiteral("profileDigestSha256"))},
      {QStringLiteral("projectNorthStarAvailable"),
       !project_north_star.isEmpty()},
      {QStringLiteral("projectNorthStarDigest"),
       project_north_star.value(QStringLiteral("digestSha256"))},
      {QStringLiteral("projectNorthStarCurrentRequestWins"), true},
      {QStringLiteral("imageIncluded"), image_included},
      {QStringLiteral("imageByteCount"), image_payload.transmitted_bytes},
      {QStringLiteral("imageOriginalByteCount"), image_payload.original_bytes},
      {QStringLiteral("imageWidth"), image_payload.pixel_size.width()},
      {QStringLiteral("imageHeight"), image_payload.pixel_size.height()},
      {QStringLiteral("imageMaxEdge"), ollama_planning_image_edge(model)},
      {QStringLiteral("planningMaxOutputTokens"),
       planning_max_output_tokens},
      {QStringLiteral("generationSchemaContract"),
       QStringLiteral("agent-operation-discriminated-generation-v2")},
      {QStringLiteral("compositionGroundingContext"), QVariantMap{
          {QStringLiteral("assetId"), composition_context.asset_id},
          {QStringLiteral("authoritativeInspectionPresent"),
           composition_context.authoritative_inspection_present},
          {QStringLiteral("layerGroundingContextPresent"),
           composition_context.layer_grounding_context_present},
          {QStringLiteral("groundedLayerIdCount"), composition_context.grounded_layer_ids.size()},
          {QStringLiteral("nativeFontInventoryPresent"),
           composition_context.native_font_inventory_present},
          {QStringLiteral("nativeFontInventoryEntryCount"),
           native_font_inventory_entry_count},
      }},
      {QStringLiteral("generationSchemaByteCount"),
       QJsonDocument(generation_schema).toJson(QJsonDocument::Compact).size()},
      {QStringLiteral("generationSchema"), generation_schema.toVariantMap()},
      {QStringLiteral("imageSourceKind"), image_payload.source_kind},
      {QStringLiteral("imageSourcePath"), image_payload.source_path},
      {QStringLiteral("additionalProofImageCount"), additional_images.payloads.size()},
      {QStringLiteral("additionalProofImagePaths"), additional_images.paths},
      {QStringLiteral("responseSchema"),
       ollama_response_payload_schema(request.allow_tool_call_plan,
                                      request.require_tool_call_plan).toVariantMap()},
  };

  if (estimated_context_tokens_before_cap > context_token_limit) {
    result.error_message = QStringLiteral(
        "The complete planning prompt exceeds the selected model's context budget after safe compaction. Reduce optional context and retry.");
    result.prompt_audit.insert(QStringLiteral("errorCode"),
        QStringLiteral("planning_context_budget_exceeded"));
    result.prompt_audit.insert(QStringLiteral("requestSent"), false);
    return result;
  }

  result.prompt_audit.insert(QStringLiteral("requestSent"), true);
  QString error_message;
  QVariantMap transport_report;
  const auto response = ollama_frozen_generation_with_output_recovery(
      endpoint, payload, ollama_request_timeout_ms(model),
      estimated_context_tokens_before_cap, context_token_limit,
      request.cancellation_requested, &error_message, &transport_report, &result.prompt_audit);
  if (response.isEmpty()) {
    result.error_message = error_message.isEmpty()
        ? QStringLiteral("Ollama did not return a response.") : error_message;
    return result;
  }
  QString response_source;
  const auto content = ollama_structured_response_text(response, &response_source);
  if (response.contains(QStringLiteral("done")) && !response.value(QStringLiteral("done")).toBool()) {
    result.error_message = QStringLiteral("Ollama returned an unfinished planning response.");
    result.prompt_audit.insert(QStringLiteral("errorCode"), QStringLiteral("planning_response_unfinished"));
    return result;
  }
  if (response.value(QStringLiteral("done_reason")).toString() == QStringLiteral("length")) {
    result.error_message = QStringLiteral("Ollama planning exhausted its output allowance without a complete generation.");
    result.prompt_audit.insert(QStringLiteral("errorCode"), QStringLiteral("planning_output_truncated"));
    return result;
  }
  const auto duration_ms = [&response](const QString& field) {
    return qRound64(response.value(field).toDouble() / 1000000.0);
  };
  result.prompt_audit.insert(
      QStringLiteral("ollamaServer"),
      QVariantMap{
          {QStringLiteral("totalDurationMs"), duration_ms(QStringLiteral("total_duration"))},
          {QStringLiteral("loadDurationMs"), duration_ms(QStringLiteral("load_duration"))},
          {QStringLiteral("promptEvalDurationMs"),
           duration_ms(QStringLiteral("prompt_eval_duration"))},
          {QStringLiteral("evalDurationMs"), duration_ms(QStringLiteral("eval_duration"))},
          {QStringLiteral("promptEvalCount"),
           response.value(QStringLiteral("prompt_eval_count")).toInt()},
          {QStringLiteral("evalCount"), response.value(QStringLiteral("eval_count")).toInt()},
      });
  result.prompt_audit.insert(
      QStringLiteral("responseTopLevelFields"), response.keys());
  result.prompt_audit.insert(
      QStringLiteral("responseError"),
      response.value(QStringLiteral("error")).toString().left(360));
  result.prompt_audit.insert(QStringLiteral("ollamaResponseSource"), response_source);
  result.prompt_audit.insert(QStringLiteral("responseCharCount"), content.size());
  result.prompt_audit.insert(
      QStringLiteral("responsePreview"), content.left(500));
  QJsonParseError parse_error;
  const auto content_document = QJsonDocument::fromJson(content.toUtf8(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !content_document.isObject()) {
    result.error_message = QStringLiteral("Ollama response content was not valid JSON.");
    return result;
  }
  const auto content_object = content_document.object();
  result.prompt_audit.insert(QStringLiteral("responseFieldNames"), content_object.keys());
  const auto response_decision =
      content_object.value(QStringLiteral("decision")).toString().trimmed();
  result.prompt_audit.insert(QStringLiteral("responseDecision"), response_decision);
  if (request.require_tool_call_plan &&
      response_decision != QStringLiteral("tool_call_plan") &&
      response_decision != QStringLiteral("needs_user_input") &&
      response_decision != QStringLiteral("cannot_complete_safely")) {
    result.error_message = QStringLiteral(
        "A tool_call_plan is required because approved composition work remains unresolved; the model returned no valid tool plan.");
    result.prompt_audit.insert(QStringLiteral("requiredToolPlanRejected"), true);
    result.prompt_audit.insert(QStringLiteral("errorCode"),
                               QStringLiteral("required_tool_plan_not_returned"));
    return result;
  }
  result.prompt_audit.insert(QStringLiteral("responseHasEditIntent"),
                             content_object.value(QStringLiteral("editIntent")).isObject() &&
                                 !content_object.value(QStringLiteral("editIntent")).toObject().isEmpty());
  result.prompt_audit.insert(QStringLiteral("responseCitationCount"),
                             content_object.value(QStringLiteral("styleConstraintCitations")).toArray().size());
  result.decision = response_decision.isEmpty() && !request.require_tool_call_plan
                        ? QStringLiteral("edit_intent")
                        : response_decision;
  result.plan_summary = content_object.value(QStringLiteral("summary")).toString();
  result.style_constraint_citations =
      content_object.value(QStringLiteral("styleConstraintCitations")).toArray().toVariantList();
  result.edit_intent = normalized_model_edit_intent(
      content_object.value(QStringLiteral("editIntent")).toObject().toVariantMap());
  const auto model_global_change_policy =
      result.edit_intent.value(QStringLiteral("globalChangePolicy")).toString();
  apply_explicit_user_intent_constraints(
      &result.edit_intent, request.user_instructions);
  const auto enforced_global_change_policy =
      result.edit_intent.value(QStringLiteral("globalChangePolicy")).toString();
  if (model_global_change_policy != enforced_global_change_policy) {
    result.prompt_audit.insert(
        QStringLiteral("globalChangePolicyCorrection"),
        QVariantMap{
            {QStringLiteral("contract"),
             QStringLiteral("agent-explicit-scope-authority-v1")},
            {QStringLiteral("modelValue"), model_global_change_policy},
            {QStringLiteral("enforcedValue"), enforced_global_change_policy},
            {QStringLiteral("currentRequestAuthoritative"), true},
        });
  }
  result.tool_call_plan =
      content_object.value(QStringLiteral("toolCalls")).toArray().toVariantList();
  if (request.allow_tool_call_plan) {
    QVariantList corrections;
    result.tool_call_plan = normalize_composition_inspection_calls(
        result.tool_call_plan, request.available_tool_schemas, &corrections);
    if (!corrections.isEmpty())
      result.prompt_audit.insert(QStringLiteral("toolCallCorrections"), corrections);
  }
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
      result.error_message = QStringLiteral("Ollama tool_call_plan response must include nonempty toolCalls.");
      return result;
    }
    QVariantList rejected_calls;
    QStringList rejection_messages;
    const auto original_calls = content_object.value(QStringLiteral("toolCalls")).toArray();
    for (qsizetype call_index = 0; call_index < original_calls.size(); ++call_index) {
      const auto call_value = original_calls.at(call_index);
      if (!call_value.isObject()) {
        result.error_message = QStringLiteral("Ollama tool_call_plan entries must include a typed tool name and object arguments.");
        return result;
      }
      const auto call = call_value.toObject();
      const auto name_value = call.value(QStringLiteral("name"));
      const auto arguments = call.value(QStringLiteral("arguments"));
      if (!name_value.isString() || name_value.toString().trimmed().isEmpty() ||
          !arguments.isObject()) {
        result.error_message = QStringLiteral("Ollama tool_call_plan entries must include a typed tool name and object arguments.");
        return result;
      }
      const auto normalized_call = call_index < result.tool_call_plan.size()
          ? result.tool_call_plan.at(call_index).toMap() : call.toVariantMap();
      const auto executed_name = normalized_call.value(QStringLiteral("name")).toString();
      const auto registered = std::find_if(
          request.available_tool_schemas.cbegin(), request.available_tool_schemas.cend(),
          [&executed_name](const QVariant& tool_value) {
            return tool_value.toMap().value(QStringLiteral("name")).toString() == executed_name;
          });
      QStringList errors;
      QJsonObject input_schema;
      QVariantMap composition_context_evidence;
      if (registered == request.available_tool_schemas.cend()) {
        errors.append(QStringLiteral("tool name is not in the supplied tool set"));
      } else {
        input_schema = QJsonObject::fromVariantMap(
            registered->toMap().value(QStringLiteral("inputSchema")).toMap());
        if (input_schema.value(QStringLiteral("type")).toString() != QStringLiteral("object")) {
          errors.append(QStringLiteral("the supplied tool has no typed object input schema"));
        } else {
          errors = json_schema_validation_errors(
              QJsonObject::fromVariantMap(
                  normalized_call.value(QStringLiteral("arguments")).toMap()),
              input_schema, QStringLiteral("arguments"));
          if (executed_name == QStringLiteral("photo.edit_composition")) {
            errors += composition_context_validation_errors(
                QJsonObject::fromVariantMap(
                    normalized_call.value(QStringLiteral("arguments")).toMap()),
                input_schema, composition_context, require_font_face_pair,
                &composition_context_evidence);
          }
        }
      }
      if (!errors.isEmpty()) {
        QVariantMap rejected{
            {QStringLiteral("index"), call_index},
            {QStringLiteral("toolName"), executed_name},
            {QStringLiteral("originalCall"), call.toVariantMap()},
            {QStringLiteral("arguments"), arguments.toObject().toVariantMap()},
            {QStringLiteral("inputSchema"), input_schema.toVariantMap()},
            {QStringLiteral("errors"), errors},
        };
        if (!composition_context_evidence.isEmpty())
          rejected.insert(QStringLiteral("compositionContextEvidence"), composition_context_evidence);
        if (normalized_call != call.toVariantMap())
          rejected.insert(QStringLiteral("normalizedCall"), normalized_call);
        rejected_calls.append(rejected);
        rejection_messages.append(QStringLiteral("Call %1 (%2): %3")
            .arg(call_index + 1).arg(executed_name, errors.join(QStringLiteral("; "))));
      }
    }
    if (!rejected_calls.isEmpty()) {
      result.prompt_audit.insert(QStringLiteral("toolCallValidation"), QVariantMap{
          {QStringLiteral("valid"), false},
          {QStringLiteral("contract"), QStringLiteral("agent-tool-call-schema-validation-v1")},
          {QStringLiteral("rejectedCalls"), rejected_calls},
          {QStringLiteral("unexecutedCalls"), original_calls.toVariantList()},
          {QStringLiteral("executionBlocked"), true},
      });
      result.error_message = QStringLiteral("Ollama tool_call_plan failed input-schema validation. %1")
          .arg(rejection_messages.join(QStringLiteral(" ")));
      result.tool_call_plan.clear();
      return result;
    }
    result.prompt_audit.insert(QStringLiteral("toolCallValidation"), QVariantMap{
        {QStringLiteral("valid"), true},
        {QStringLiteral("contract"), QStringLiteral("agent-tool-call-schema-validation-v1")},
        {QStringLiteral("validatedCallCount"), result.tool_call_plan.size()},
        {QStringLiteral("executionBlocked"), false},
    });
    result.ok = true;
    return result;
  }
  if (result.decision == QStringLiteral("needs_user_input")) {
    const auto questions = content_object.value(QStringLiteral("userQuestions")).toArray();
    const auto has_valid_question = std::any_of(
        questions.cbegin(), questions.cend(), [](const QJsonValue& value) {
          return value.isString() && !value.toString().trimmed().isEmpty();
        });
    if (!has_valid_question) {
      result.error_message = QStringLiteral("Ollama needs_user_input response must include nonempty string userQuestions.");
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
  if (result.decision.isEmpty() || result.decision == QStringLiteral("edit_intent")) {
    result.decision = QStringLiteral("edit_intent");
  }
  if (result.decision != QStringLiteral("edit_intent")) {
    result.error_message = QStringLiteral("Unsupported Ollama planning decision: %1").arg(result.decision);
    return result;
  }
  if (result.style_constraint_citations.isEmpty()) {
    result.error_message = QStringLiteral("Ollama edit_intent response must include style citations.");
    return result;
  }
  const auto raw_intent = content_object.value(QStringLiteral("editIntent"));
  const auto raw_intent_object = raw_intent.toObject();
  const auto raw_policy = raw_intent_object.value(QStringLiteral("globalChangePolicy"));
  const auto legacy_non_tool_policy_omission =
      !request.allow_tool_call_plan &&
      !raw_intent_object.contains(QStringLiteral("globalChangePolicy"));
  const auto optional_array_is_typed = [&raw_intent_object](const QString& field) {
    const auto value = raw_intent_object.value(field);
    return !raw_intent_object.contains(field) || value.isArray();
  };
  const auto typed_intent = raw_intent.isObject() && !raw_intent_object.isEmpty() &&
      raw_intent_object.value(QStringLiteral("goalSummary")).isString() &&
      !raw_intent_object.value(QStringLiteral("goalSummary")).toString().trimmed().isEmpty() &&
      raw_intent_object.value(QStringLiteral("confidence")).isDouble() &&
      (legacy_non_tool_policy_omission ||
       (raw_policy.isString() &&
        (raw_policy.toString() == QStringLiteral("allow") ||
         raw_policy.toString() == QStringLiteral("preserve")))) &&
      optional_array_is_typed(QStringLiteral("visualFindings")) &&
      optional_array_is_typed(QStringLiteral("localizedActions")) &&
      optional_array_is_typed(QStringLiteral("retouchActions"));
  if (!typed_intent || result.edit_intent.isEmpty()) {
    result.error_message = QStringLiteral("Ollama edit_intent response must include a typed, nonempty editIntent.");
    return result;
  }
  const auto raw_retouch_actions = content_object
                                       .value(QStringLiteral("editIntent"))
                                       .toObject()
                                       .value(QStringLiteral("retouchActions"))
                                       .toArray();
  const auto retouch_actions =
      result.edit_intent.value(QStringLiteral("retouchActions")).toList();
  if (!raw_retouch_actions.isEmpty() && retouch_actions.isEmpty()) {
    result.decision = QStringLiteral("needs_user_input");
    result.user_questions = QVariantList{
        QStringLiteral(
            "Which small, clearly visible spot or distraction should I retouch?"),
    };
    result.prompt_audit.insert(
        QStringLiteral("retouchIntentRejected"),
        QStringLiteral("missing-bounded-high-confidence-evidence"));
    result.ok = true;
    return result;
  }
  if (!retouch_actions.isEmpty() &&
      !style_tool_enabled(request.style, QStringLiteral("retouch"))) {
    result.decision = QStringLiteral("cannot_complete_safely");
    result.safe_stop_reason = QStringLiteral(
        "The active style does not allow retouching, so the agent did not create a cleanup draft.");
    result.prompt_audit.insert(
        QStringLiteral("retouchIntentRejected"),
        QStringLiteral("style-permission-disabled"));
    result.ok = true;
    return result;
  }
  // A preserve/no-op answer may close a task only after this request has
  // already applied native composition edits and read those exact IDs back.
  // Empty first-turn edit intents and forced repair turns still fail closed.
  if (request.allow_tool_call_plan && !request.require_tool_call_plan &&
      result.edit_intent.value(QStringLiteral("completionStatus")).toString() ==
          QStringLiteral("complete") &&
      result.edit_intent.value(QStringLiteral("globalChangePolicy")).toString() ==
          QStringLiteral("preserve") &&
      edit_intent_keeps_global_controls_neutral(result.edit_intent) &&
      result.edit_intent.value(QStringLiteral("localizedActions")).toList().isEmpty() &&
      retouch_actions.isEmpty()) {
    const auto completion_evidence = prior_native_composition_completion_evidence(request);
    if (!completion_evidence.isEmpty()) {
      result.prompt_audit.insert(QStringLiteral("nativeCompositionCompletion"), completion_evidence);
      result.prompt_audit.insert(QStringLiteral("modelDecision"), QStringLiteral("edit_intent"));
      result.prompt_audit.insert(QStringLiteral("completionMode"),
          QStringLiteral("preserve-after-native-composition-readback"));
      result.decision = QStringLiteral("recipe_patch");
      result.ok = true;
      return result;
    }
  }
  auto compile_request = request;
  compile_request.model_id = QString::fromLatin1(kRulebookDeterministicModelId);
  compile_request.allow_tool_call_plan = false;
  compile_request.available_tool_schemas.clear();
  compile_request.prior_tool_results.clear();
  compile_request.user_instructions = canonical_instructions_for_edit_intent(result.edit_intent);
  const auto compiled = plan_agent_recipe_patch(compile_request);
  if (!compiled.ok || compiled.recipe_patch.isEmpty()) {
    result.error_message = compiled.error_message.trimmed().isEmpty()
                               ? QStringLiteral("The app could not compile the model edit intent into a safe recipe.")
                               : compiled.error_message;
    return result;
  }
  result.recipe_patch =
      result.edit_intent.value(QStringLiteral("globalChangePolicy")).toString() ==
              QStringLiteral("preserve")
          ? QVariantMap{}
          : compiled.recipe_patch;
  const auto localized_masks =
      localized_mask_stack_from_intent(result.edit_intent, request.style);
  if (!localized_masks.isEmpty()) {
    result.recipe_patch.insert(
        QStringLiteral("masks"),
        QVariantMap{
            {QStringLiteral("appendMaskStack"), localized_masks},
        });
  }
  const auto retouch_stack =
      retouch_stack_from_intent(result.edit_intent, request.style);
  if (!retouch_stack.isEmpty()) {
    result.recipe_patch.insert(
        QStringLiteral("retouch"),
        QVariantMap{
            {QStringLiteral("appendRetouchStack"), retouch_stack},
        });
  }
  if (result.recipe_patch.isEmpty()) {
    result.error_message = QStringLiteral(
        "The user asked to preserve global appearance, but the model did not provide a safe localized action.");
    return result;
  }
  result.prompt_audit.insert(QStringLiteral("modelDecision"), QStringLiteral("edit_intent"));
  result.prompt_audit.insert(QStringLiteral("intentCompiler"), QStringLiteral("rulebook-deterministic-v2"));
  result.prompt_audit.insert(QStringLiteral("canonicalIntentInstructions"), compile_request.user_instructions);
  result.prompt_audit.insert(QStringLiteral("compiledRecipeGroupCount"), result.recipe_patch.size());
  result.prompt_audit.insert(
      QStringLiteral("visualFindingCount"),
      result.edit_intent.value(QStringLiteral("visualFindings")).toList().size());
  result.prompt_audit.insert(
      QStringLiteral("localizedActionCount"),
      result.edit_intent.value(QStringLiteral("localizedActions")).toList().size());
  result.prompt_audit.insert(
      QStringLiteral("localizedMaskCount"), localized_masks.size());
  result.prompt_audit.insert(
      QStringLiteral("localizedMaskWriteMode"),
      localized_masks.isEmpty() ? QStringLiteral("none")
                                : QStringLiteral("append-preserve-existing"));
  result.prompt_audit.insert(
      QStringLiteral("retouchActionCount"), retouch_actions.size());
  result.prompt_audit.insert(
      QStringLiteral("retouchWriteMode"),
      retouch_stack.isEmpty() ? QStringLiteral("none")
                              : QStringLiteral("append-preserve-existing"));
  result.prompt_audit.insert(
      QStringLiteral("globalChangePolicy"),
      result.edit_intent.value(QStringLiteral("globalChangePolicy")));
  result.decision = QStringLiteral("recipe_patch");
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

struct FastVisualRegionHint final {
  QString label;
  double minimum_x = 0.0;
  double maximum_x = 1.0;
  double minimum_y = 0.0;
  double maximum_y = 1.0;
};

FastVisualRegionHint fast_visual_region_hint(const QString& instructions) {
  auto normalized = instructions.trimmed().toLower();
  normalized.replace(QLatin1Char('-'), QLatin1Char(' '));
  if (normalized.contains(QStringLiteral("upper right")) ||
      normalized.contains(QStringLiteral("top right"))) {
    return {QStringLiteral("upper-right"), 0.5, 1.0, 0.0, 0.5};
  }
  if (normalized.contains(QStringLiteral("upper left")) ||
      normalized.contains(QStringLiteral("top left"))) {
    return {QStringLiteral("upper-left"), 0.0, 0.5, 0.0, 0.5};
  }
  if (normalized.contains(QStringLiteral("lower right")) ||
      normalized.contains(QStringLiteral("bottom right"))) {
    return {QStringLiteral("lower-right"), 0.5, 1.0, 0.5, 1.0};
  }
  if (normalized.contains(QStringLiteral("lower left")) ||
      normalized.contains(QStringLiteral("bottom left"))) {
    return {QStringLiteral("lower-left"), 0.0, 0.5, 0.5, 1.0};
  }
  return {};
}

QString fast_visual_asset_path(const QVariantMap& asset) {
  const QStringList candidate_keys{
      QStringLiteral("proofImagePath"),
      QStringLiteral("currentProofPath"),
      QStringLiteral("afterProofPath"),
      QStringLiteral("beforeProofPath"),
      QStringLiteral("filePath"),
  };
  for (const auto& key : candidate_keys) {
    const auto path = asset.value(key).toString().trimmed();
    if (!path.isEmpty() && QFileInfo::exists(path)) {
      return path;
    }
  }
  return {};
}

AgentLocalModelPlanResult fast_local_highlight_plan(
    const AgentLocalModelPlanRequest& request) {
  AgentLocalModelPlanResult result;
  const auto instructions = request.user_instructions.trimmed().toLower();
  const auto region_hint = fast_visual_region_hint(instructions);
  const auto asks_for_local_only =
      instructions.contains(QStringLiteral("only")) ||
      instructions.contains(QStringLiteral("leave the rest unchanged")) ||
      instructions.contains(QStringLiteral("remain unchanged")) ||
      instructions.contains(QStringLiteral("unaffected regions"));
  const auto identifies_bright_region =
      instructions.contains(QStringLiteral("bright")) ||
      instructions.contains(QStringLiteral("highlight")) ||
      instructions.contains(QStringLiteral("glare")) ||
      instructions.contains(QStringLiteral("overexposed"));
  const auto asks_to_reduce =
      instructions.contains(QStringLiteral("subdue")) ||
      instructions.contains(QStringLiteral("reduce")) ||
      instructions.contains(QStringLiteral("tone down")) ||
      instructions.contains(QStringLiteral("darken")) ||
      instructions.contains(QStringLiteral("pull back")) ||
      instructions.contains(QStringLiteral("recover"));
  if (request.force_model_planning || request.allow_tool_call_plan ||
      !asks_for_local_only || !identifies_bright_region || !asks_to_reduce ||
      region_hint.label.isEmpty() ||
      !style_tool_enabled(request.style, QStringLiteral("masks"))) {
    return result;
  }

  const auto source_path = fast_visual_asset_path(request.asset);
  QImage source(source_path);
  if (source.isNull()) {
    return result;
  }
  const auto analysis = source.scaled(
      160,
      160,
      Qt::KeepAspectRatio,
      Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
  const auto x_begin = std::clamp(
      static_cast<int>(std::floor(region_hint.minimum_x * analysis.width())),
      0,
      analysis.width() - 1);
  const auto x_end = std::clamp(
      static_cast<int>(std::ceil(region_hint.maximum_x * analysis.width())),
      x_begin + 1,
      analysis.width());
  const auto y_begin = std::clamp(
      static_cast<int>(std::floor(region_hint.minimum_y * analysis.height())),
      0,
      analysis.height() - 1);
  const auto y_end = std::clamp(
      static_cast<int>(std::ceil(region_hint.maximum_y * analysis.height())),
      y_begin + 1,
      analysis.height());
  const auto luminance = [](const QColor& color) {
    return 0.2126 * color.redF() * 255.0 +
           0.7152 * color.greenF() * 255.0 +
           0.0722 * color.blueF() * 255.0;
  };
  QVector<double> regional_luminance;
  regional_luminance.reserve((x_end - x_begin) * (y_end - y_begin));
  auto image_luminance_sum = 0.0;
  for (auto y = 0; y < analysis.height(); ++y) {
    for (auto x = 0; x < analysis.width(); ++x) {
      const auto value = luminance(analysis.pixelColor(x, y));
      image_luminance_sum += value;
      if (x >= x_begin && x < x_end && y >= y_begin && y < y_end) {
        regional_luminance.append(value);
      }
    }
  }
  if (regional_luminance.size() < 16) {
    return result;
  }
  std::sort(regional_luminance.begin(), regional_luminance.end());
  const auto percentile_index = std::clamp(
      static_cast<int>(regional_luminance.size() * 0.84),
      0,
      static_cast<int>(regional_luminance.size()) - 1);
  const auto threshold = std::max(210.0, regional_luminance.at(percentile_index));
  auto weight_sum = 0.0;
  auto weighted_x = 0.0;
  auto weighted_y = 0.0;
  auto selected_luminance_sum = 0.0;
  auto selected_count = 0;
  auto peak_luminance = 0.0;
  for (auto y = y_begin; y < y_end; ++y) {
    for (auto x = x_begin; x < x_end; ++x) {
      const auto value = luminance(analysis.pixelColor(x, y));
      peak_luminance = std::max(peak_luminance, value);
      if (value < threshold) {
        continue;
      }
      const auto weight = value - threshold + 1.0;
      weight_sum += weight;
      weighted_x += (static_cast<double>(x) + 0.5) * weight;
      weighted_y += (static_cast<double>(y) + 0.5) * weight;
      selected_luminance_sum += value;
      ++selected_count;
    }
  }
  if (selected_count < 8 || weight_sum <= 0.0) {
    return result;
  }
  const auto center_x_pixels = weighted_x / weight_sum;
  const auto center_y_pixels = weighted_y / weight_sum;
  auto variance_x = 0.0;
  auto variance_y = 0.0;
  for (auto y = y_begin; y < y_end; ++y) {
    for (auto x = x_begin; x < x_end; ++x) {
      const auto value = luminance(analysis.pixelColor(x, y));
      if (value < threshold) {
        continue;
      }
      const auto weight = value - threshold + 1.0;
      variance_x += std::pow((static_cast<double>(x) + 0.5) - center_x_pixels, 2.0) * weight;
      variance_y += std::pow((static_cast<double>(y) + 0.5) - center_y_pixels, 2.0) * weight;
    }
  }
  variance_x /= weight_sum;
  variance_y /= weight_sum;
  const auto mean_luminance =
      image_luminance_sum /
      static_cast<double>(analysis.width() * analysis.height());
  const auto selected_luminance =
      selected_luminance_sum / static_cast<double>(selected_count);
  const auto luminance_separation = selected_luminance - mean_luminance;
  if (peak_luminance < 225.0 || luminance_separation < 34.0) {
    return result;
  }

  const auto center_x = std::clamp(
      center_x_pixels / static_cast<double>(analysis.width()), 0.0, 1.0);
  const auto center_y = std::clamp(
      center_y_pixels / static_cast<double>(analysis.height()), 0.0, 1.0);
  const auto radius_x = std::clamp(
      2.35 * std::sqrt(variance_x) / static_cast<double>(analysis.width()) + 0.035,
      0.07,
      0.34);
  const auto radius_y = std::clamp(
      2.35 * std::sqrt(variance_y) / static_cast<double>(analysis.height()) + 0.035,
      0.07,
      0.34);
  const auto confidence = std::clamp(0.68 + luminance_separation / 420.0, 0.68, 0.96);
  const auto exposure = -std::clamp((selected_luminance - 180.0) / 120.0, 0.24, 0.62);
  const auto highlights = -std::clamp((selected_luminance - 175.0) * 0.48, 18.0, 44.0);
  const QVariantMap finding{
      {QStringLiteral("kind"), QStringLiteral("highlight_clipping")},
      {QStringLiteral("target"), QStringLiteral("background")},
      {QStringLiteral("severity"),
       peak_luminance >= 248.0 ? QStringLiteral("high")
                               : QStringLiteral("medium")},
      {QStringLiteral("observation"),
       QStringLiteral("A bright %1 background region is pulling attention from the subject.")
           .arg(region_hint.label)},
      {QStringLiteral("confidence"), confidence},
      {QStringLiteral("region"),
       QVariantMap{
           {QStringLiteral("centerX"), center_x},
           {QStringLiteral("centerY"), center_y},
           {QStringLiteral("radiusX"), radius_x},
           {QStringLiteral("radiusY"), radius_y},
       }},
  };
  const QVariantMap action{
      {QStringLiteral("target"), QStringLiteral("background")},
      {QStringLiteral("maskShape"), QStringLiteral("radial")},
      {QStringLiteral("selectionMode"), QStringLiteral("region")},
      {QStringLiteral("reason"),
       QStringLiteral("Subdue the isolated bright region while preserving the rest of the photograph.")},
      {QStringLiteral("confidence"), confidence},
      {QStringLiteral("region"),
       QVariantMap{
           {QStringLiteral("centerX"), center_x},
           {QStringLiteral("centerY"), center_y},
           {QStringLiteral("radiusX"), radius_x},
           {QStringLiteral("radiusY"), radius_y},
           {QStringLiteral("feather"), 0.72},
       }},
      {QStringLiteral("adjustments"),
       QVariantMap{
           {QStringLiteral("exposure"), exposure},
           {QStringLiteral("highlights"), highlights},
       }},
  };
  result.model_id = QString::fromLatin1(kFastLocalVisionPlannerId);
  result.decision = QStringLiteral("recipe_patch");
  result.plan_summary = QStringLiteral(
      "I found the isolated bright %1 region and prepared a feathered local correction without changing the rest of the image.")
                            .arg(region_hint.label);
  result.style_constraint_citations = cited_style_constraints(request.style);
  result.edit_intent = normalized_model_edit_intent(QVariantMap{
      {QStringLiteral("goalSummary"), result.plan_summary},
      {QStringLiteral("colorMode"), QStringLiteral("preserve")},
      {QStringLiteral("crop"), QStringLiteral("preserve")},
      {QStringLiteral("confidence"), confidence},
      {QStringLiteral("globalChangePolicy"), QStringLiteral("preserve")},
      {QStringLiteral("visualFindings"), QVariantList{finding}},
      {QStringLiteral("localizedActions"), QVariantList{action}},
  });
  const auto masks = localized_mask_stack_from_intent(result.edit_intent, request.style);
  if (masks.isEmpty()) {
    return {};
  }
  result.recipe_patch = QVariantMap{
      {QStringLiteral("masks"),
       QVariantMap{{QStringLiteral("appendMaskStack"), masks}}},
  };
  result.prompt_audit = QVariantMap{
      {QStringLiteral("backend"), QStringLiteral("local-vision")},
      {QStringLiteral("plannerMode"), QStringLiteral("fast-local-highlight")},
      {QStringLiteral("plannerDisplayName"), QStringLiteral("Fast local vision")},
      {QStringLiteral("modelInvoked"), false},
      {QStringLiteral("requestedModelId"), request.model_id},
      {QStringLiteral("fallbackUsed"), false},
      {QStringLiteral("intentCompiler"), QStringLiteral("rulebook-deterministic-v2")},
      {QStringLiteral("imageIncluded"), true},
      {QStringLiteral("imageSourcePath"), source_path},
      {QStringLiteral("imageWidth"), source.width()},
      {QStringLiteral("imageHeight"), source.height()},
      {QStringLiteral("analysisWidth"), analysis.width()},
      {QStringLiteral("analysisHeight"), analysis.height()},
      {QStringLiteral("regionHint"), region_hint.label},
      {QStringLiteral("peakLuminance"), peak_luminance},
      {QStringLiteral("meanLuminance"), mean_luminance},
      {QStringLiteral("selectedLuminance"), selected_luminance},
      {QStringLiteral("visualFindingCount"), 1},
      {QStringLiteral("localizedActionCount"), 1},
      {QStringLiteral("localizedMaskCount"), masks.size()},
      {QStringLiteral("localizedMaskWriteMode"),
       QStringLiteral("append-preserve-existing")},
      {QStringLiteral("globalChangePolicy"), QStringLiteral("preserve")},
      {QStringLiteral("transport"),
       QVariantMap{
           {QStringLiteral("ok"), true},
           {QStringLiteral("durationMs"), 0},
           {QStringLiteral("path"), QStringLiteral("local-memory")},
       }},
  };
  result.ok = true;
  return result;
}

}  // namespace

QVariantMap agent_detail::approved_typography_role_copy(
    const QVariantList& plan_actions, const QVariantList& expected_copy) {
  const QStringList roles{QStringLiteral("headline"), QStringLiteral("kicker"), QStringLiteral("footer")};
  const QRegularExpression primary_role(QStringLiteral("\\b(headline|kicker|footer)\\b"),
                                        QRegularExpression::CaseInsensitiveOption);
  QVariantMap bindings;
  QSet<QString> ambiguous_roles;
  for (const auto& action_value : plan_actions) {
    const auto action = action_value.toMap();
    auto summary = action.value(QStringLiteral("summary")).toString();
    auto copy = action.value(QStringLiteral("text"), action.value(QStringLiteral("copy"))).toString();
    QStringList matches;
    for (const auto& value : expected_copy) {
      const auto expected = value.toString();
      if (expected.trimmed().isEmpty()) continue;
      if ((!copy.isEmpty() && copy == expected) ||
          (copy.isEmpty() && summary.contains(expected, Qt::CaseInsensitive))) matches.append(expected);
      // Role words inside the actual copy are glyph content, not role claims.
      summary.replace(expected, QString(), Qt::CaseInsensitive);
    }
    matches.removeDuplicates();
    if (matches.size() != 1) continue;
    copy = matches.first();
    auto role = action.value(QStringLiteral("role")).toString().trimmed().toLower();
    if (!roles.contains(role)) role = primary_role.match(summary).captured(1).toLower();
    if (role.isEmpty() || ambiguous_roles.contains(role)) continue;
    if (bindings.contains(role) && bindings.value(role).toString() != copy) {
      bindings.remove(role);
      ambiguous_roles.insert(role);
    } else {
      bindings.insert(role, copy);
    }
  }
  // A comparative mention cannot make one copy serve multiple distinct roles.
  for (const auto& role : roles) {
    if (!bindings.contains(role)) continue;
    for (const auto& other : roles) {
      if (role != other && bindings.contains(other) && bindings.value(role) == bindings.value(other)) {
        ambiguous_roles.insert(role);
        ambiguous_roles.insert(other);
      }
    }
  }
  for (const auto& role : ambiguous_roles) bindings.remove(role);
  return bindings;
}

QVariantMap agent_detail::typography_hierarchy_fit_guidance(
    const QVariantMap& headline_layer, double minimum_ratio) {
  const auto layout = agent_graphic_proof_layout_metrics(headline_layer);
  const auto bounds = layout.value(QStringLiteral("renderedBounds")).toMap();
  const auto width = bounds.value(QStringLiteral("width")).toDouble();
  const auto left = bounds.value(QStringLiteral("x")).toDouble();
  const auto canvas_width = layout.value(QStringLiteral("canvasWidth")).toDouble();
  const auto size = layout.value(QStringLiteral("effectiveTypographySize")).toDouble();
  const auto rotation = layout.value(QStringLiteral("rotation")).toDouble();
  if (!layout.value(QStringLiteral("available")).toBool() || !std::isfinite(minimum_ratio) ||
      minimum_ratio <= 0.0 || std::abs(rotation) > 1e-7 || left < 0.0 ||
      width <= 0.0 || size <= 0.0 || canvas_width <= 2.0 * left)
    return {{QStringLiteral("available"), false}};
  const auto available_width = canvas_width - 2.0 * left;
  const auto factor = std::min(1.0, available_width / width);
  return {
      {QStringLiteral("available"), true},
      {QStringLiteral("headlineLayerId"), headline_layer.value(QStringLiteral("id"))},
      {QStringLiteral("currentRenderedWidth"), width},
      {QStringLiteral("proposedSymmetricMargin"), left},
      {QStringLiteral("availableWidthAtProposedMargins"), available_width},
      {QStringLiteral("proportionalWidthScaleProxy"), factor},
      {QStringLiteral("headlineEffectiveSizeProxy"), size * factor},
      {QStringLiteral("smallerRoleEffectiveSizeUpperProxy"), size * factor / minimum_ratio},
      {QStringLiteral("approximationOnly"), true},
      {QStringLiteral("basis"), QStringLiteral(
          "Current unrotated layer raster width at its measured left margin; proposed equal right margin. Proportional type-size estimate only, not exact glyph/effect fit. Font, tracking, scale and position changes require fresh native measurements and visible review.")},
      {QStringLiteral("strategy"), QStringLiteral(
          "If smaller-role sizes force a headline wider than available space, reduce headline and dependent smaller roles together when not protected by the brief. Do not enlarge a clipped headline solely to restore its ratio. The model chooses actual sizes and tools; this is not an edit.")},
  };
}

QVariantList agent_detail::ollama_image_context_reservations(
    const QString& model_name,
    const QList<QSize>& transmitted_sizes,
    int attached_image_count) {
  return ollama_image_context_reservation_rows(
      model_name, transmitted_sizes, attached_image_count);
}

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
  const auto configured_models = models;
  for (const auto& value : configured_models) {
    const auto managed = value.toMap();
    if (managed.value(QStringLiteral("runtime")).toString() != QStringLiteral("managed") ||
        managed.value(QStringLiteral("backend")).toString() != QStringLiteral("ollama") ||
        managed.value(QStringLiteral("model")).toString().isEmpty()) continue;
    const auto model_name = managed.value(QStringLiteral("model")).toString();
    const auto existing_id = managed.value(QStringLiteral("id")).toString() + QStringLiteral("-existing-ollama");
    const auto already_configured = std::any_of(models.cbegin(), models.cend(), [&](const QVariant& candidate) {
      const auto entry = candidate.toMap();
      return entry.value(QStringLiteral("id")).toString() == existing_id ||
          (entry.value(QStringLiteral("runtime")).toString() == QStringLiteral("external") &&
           entry.value(QStringLiteral("model")).toString() == model_name);
    });
    if (already_configured) continue;
    auto existing = managed;
    existing.insert(QStringLiteral("id"), existing_id);
    existing.insert(QStringLiteral("name"), managed.value(QStringLiteral("name")).toString() + QStringLiteral(" (existing Ollama)"));
    existing.insert(QStringLiteral("runtime"), QStringLiteral("external"));
    existing.insert(QStringLiteral("endpoint"), QStringLiteral("http://127.0.0.1:11434"));
    existing.insert(QStringLiteral("existingOllamaCandidate"), true);
    existing.remove(QStringLiteral("runtimeState"));
    existing.remove(QStringLiteral("artifactVerification"));
    existing.remove(QStringLiteral("artifactLicense"));
    existing.remove(QStringLiteral("artifactBytes"));
    const auto probe = existing_ollama_probe(existing);
    existing.insert(QStringLiteral("available"), probe.value(QStringLiteral("available"), false));
    existing.insert(QStringLiteral("status"), probe.value(QStringLiteral("status"), QStringLiteral("unavailable")));
    existing.insert(QStringLiteral("statusReason"), probe.value(QStringLiteral("statusReason"),
        probe.value(QStringLiteral("available")).toBool() ? QString() :
        QStringLiteral("Check Ollama to use the model already on this Mac.")));
    models.append(existing);
  }
  return models;
}

QString default_agent_local_model_id() {
  const auto models = available_agent_local_models();
  for (const auto& model_value : models) {
    const auto model = model_value.toMap();
    if (!model.value(QStringLiteral("taskPlanningProtocol")).toString().isEmpty() &&
        (model.value(QStringLiteral("available")).toBool() || model.value(QStringLiteral("runtime")).toString() == QStringLiteral("managed")) &&
        !model.value(QStringLiteral("id")).toString().trimmed().isEmpty()) {
      return model.value(QStringLiteral("id")).toString();
    }
  }
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
  if (model.value(QStringLiteral("runtime")).toString() == QStringLiteral("managed") &&
      !model.value(QStringLiteral("available")).toBool()) {
    report.insert(QStringLiteral("ok"), false);
    return report;
  }
  if (model_name.isEmpty()) {
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("available"), false);
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("statusReason"), QStringLiteral("Ollama model name is missing."));
    return report;
  }

  QString error_message;
  QVariantMap transport_report;
  const auto response = http_post_json(
      endpoint,
      QStringLiteral("/api/show"),
      QJsonObject{{QStringLiteral("model"), model_name}},
      5000,
      &error_message,
      &transport_report);
  report.insert(QStringLiteral("transport"), transport_report);
  if (response.isEmpty()) {
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("available"), false);
    report.insert(QStringLiteral("status"), QStringLiteral("unavailable"));
    report.insert(
        QStringLiteral("statusReason"),
        error_message.isEmpty()
            ? QStringLiteral("Ollama model probe did not return a response.")
            : error_message);
    remember_existing_ollama_probe(model, report);
    return report;
  }

  report.insert(QStringLiteral("available"), true);
  report.insert(QStringLiteral("status"), QStringLiteral("available"));
  report.remove(QStringLiteral("statusReason"));
  remember_existing_ollama_probe(model, report);
  report.insert(QStringLiteral("probeResponse"), response.toVariantMap());
  const auto details = response.value(QStringLiteral("details")).toObject();
  if (!details.isEmpty()) {
    report.insert(QStringLiteral("modelFamily"), details.value(QStringLiteral("family")).toString());
    report.insert(QStringLiteral("parameterSize"), details.value(QStringLiteral("parameter_size")).toString());
    report.insert(QStringLiteral("quantizationLevel"), details.value(QStringLiteral("quantization_level")).toString());
  }
  return report;
}

AgentLocalModelTaskSessionResult plan_agent_task_session(
    const AgentLocalModelTaskSessionRequest& request) {
  AgentLocalModelTaskSessionResult result;
  result.model_id = request.model_id.trimmed().isEmpty()
                        ? default_agent_local_model_id()
                        : request.model_id.trimmed();
  const auto model = model_metadata_by_id(result.model_id);
  if (model.isEmpty()) {
    result.error_message = QStringLiteral("Unsupported local model: %1").arg(result.model_id);
    return result;
  }
  auto protocol = model.value(QStringLiteral("taskPlanningProtocol")).toString().trimmed().toLower();
  if (protocol.isEmpty() && model.value(QStringLiteral("backend")).toString() == QStringLiteral("ollama")) {
    protocol = QStringLiteral("ollama-chat");
  }
  if (protocol != QStringLiteral("ollama-chat")) {
    result.error_message = QStringLiteral(
        "The selected model has no supported local structured-chat transport. Built-in deterministic planners cannot replace conversational planning.");
    return result;
  }
  if (!model.value(QStringLiteral("available")).toBool()) {
    result.error_message = model.value(QStringLiteral("statusReason")).toString().trimmed();
    if (result.error_message.isEmpty()) {
      result.error_message = QStringLiteral("The selected local model is unavailable. Check its local runtime and retry.");
    }
    return result;
  }
  if (request.cancellation_requested && request.cancellation_requested()) {
    result.error_message = QStringLiteral("Planning canceled.");
    return result;
  }
  const auto prompt_text = request.brief.value(QStringLiteral("prompt")).toString().trimmed();
  if (prompt_text.isEmpty()) {
    result.error_message = QStringLiteral("Describe what you want the agent to do before planning.");
    return result;
  }

  const auto use_defaults = request.brief.value(QStringLiteral("useDefaults")).toBool() ||
                            request.brief.value(QStringLiteral("clarificationComplete")).toBool();
  const QJsonObject option_schema{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("properties"), QJsonObject{
          {QStringLiteral("id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
          {QStringLiteral("label"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
      }},
      {QStringLiteral("required"), QJsonArray{QStringLiteral("id"), QStringLiteral("label")}},
      {QStringLiteral("additionalProperties"), false},
  };
  const QJsonObject question_schema{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("properties"), QJsonObject{
          {QStringLiteral("id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
          {QStringLiteral("prompt"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
          {QStringLiteral("reason"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
          {QStringLiteral("required"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
          {QStringLiteral("inputType"), QJsonObject{
              {QStringLiteral("type"), QStringLiteral("string")},
              {QStringLiteral("enum"), QJsonArray{QStringLiteral("text"), QStringLiteral("choice")}},
          }},
          {QStringLiteral("options"), QJsonObject{
              {QStringLiteral("type"), QStringLiteral("array")},
              {QStringLiteral("items"), option_schema},
          }},
      }},
      {QStringLiteral("required"), QJsonArray{
          QStringLiteral("id"), QStringLiteral("prompt"), QStringLiteral("reason"),
          QStringLiteral("required"), QStringLiteral("inputType"), QStringLiteral("options")}},
      {QStringLiteral("additionalProperties"), false},
  };
  const QJsonObject exact_copy_schema{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("properties"), QJsonObject{
          {QStringLiteral("text"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
          {QStringLiteral("source"), QJsonObject{
              {QStringLiteral("type"), QStringLiteral("string")},
              {QStringLiteral("enum"), QJsonArray{
                  QStringLiteral("prompt"), QStringLiteral("answer"), QStringLiteral("context"),
                  QStringLiteral("proposed")}},
          }},
      }},
      {QStringLiteral("required"), QJsonArray{QStringLiteral("text"), QStringLiteral("source")}},
      {QStringLiteral("additionalProperties"), false},
  };
  QJsonArray required_plan_fields{
      QStringLiteral("summary"), QStringLiteral("outcome"), QStringLiteral("exactCopy"),
      QStringLiteral("deliverables"), QStringLiteral("constraints"), QStringLiteral("actions")};
  if (request.context.value(QStringLiteral("blankCanvas")).toBool())
    required_plan_fields.append(QStringLiteral("canvasSettings"));
  const QJsonObject plan_schema{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("properties"), QJsonObject{
          {QStringLiteral("summary"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
          {QStringLiteral("outcome"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
          {QStringLiteral("canvasSettings"), QJsonObject{
              {QStringLiteral("type"), QStringLiteral("object")},
              {QStringLiteral("properties"), QJsonObject{
                  {QStringLiteral("name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                  {QStringLiteral("width"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 8192}}},
                  {QStringLiteral("height"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 8192}}},
                  {QStringLiteral("resolution"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 72}, {QStringLiteral("maximum"), 600}}},
                  {QStringLiteral("background"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{"transparent", "white", "color"}}}},
                  {QStringLiteral("color"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
              }},
              {QStringLiteral("required"), QJsonArray{"name", "width", "height", "resolution", "background", "color"}},
              {QStringLiteral("additionalProperties"), false},
          }},
          {QStringLiteral("actions"), QJsonObject{
              {QStringLiteral("type"), QStringLiteral("array")},
              {QStringLiteral("minItems"), 1},
              {QStringLiteral("maxItems"), 6},
              {QStringLiteral("items"), QJsonObject{
                  {QStringLiteral("type"), QStringLiteral("object")},
                  {QStringLiteral("properties"), QJsonObject{
                      {QStringLiteral("kind"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                          {QStringLiteral("enum"), QJsonArray{"color", "framing", "masking", "text", "layers", "effects"}}}},
                      {QStringLiteral("summary"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                          {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 180}}},
                  }},
                  {QStringLiteral("required"), QJsonArray{"kind", "summary"}},
                  {QStringLiteral("additionalProperties"), false},
              }},
          }},
          {QStringLiteral("exactCopy"), QJsonObject{
              {QStringLiteral("type"), QStringLiteral("array")},
              {QStringLiteral("items"), exact_copy_schema},
          }},
          {QStringLiteral("deliverables"), QJsonObject{
              {QStringLiteral("type"), QStringLiteral("array")},
              {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
          }},
          {QStringLiteral("constraints"), QJsonObject{
              {QStringLiteral("type"), QStringLiteral("array")},
              {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
          }},
      }},
      {QStringLiteral("required"), required_plan_fields},
      {QStringLiteral("additionalProperties"), false},
  };
  const QJsonObject schema{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("properties"), QJsonObject{
          {QStringLiteral("decision"), QJsonObject{
              {QStringLiteral("type"), QStringLiteral("string")},
              {QStringLiteral("enum"), use_defaults
                  ? QJsonArray{QStringLiteral("plan_ready"), QStringLiteral("cannot_complete")}
                  : QJsonArray{QStringLiteral("ask_questions"), QStringLiteral("plan_ready"),
                               QStringLiteral("cannot_complete")}},
          }},
          {QStringLiteral("activity"), QJsonObject{
              {QStringLiteral("type"), QStringLiteral("string")},
              {QStringLiteral("enum"), QJsonArray{
                  QStringLiteral("Reviewing the request"),
                  QStringLiteral("Checking the selected targets"),
                  QStringLiteral("Identifying essential missing details"),
                  QStringLiteral("Preparing the requested outcome")}},
          }},
          {QStringLiteral("questions"), QJsonObject{
              {QStringLiteral("type"), QStringLiteral("array")},
              {QStringLiteral("items"), question_schema},
              {QStringLiteral("maxItems"), use_defaults ? 0 : 2},
          }},
          {QStringLiteral("plan"), plan_schema},
          {QStringLiteral("reason"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
          {QStringLiteral("warnings"), QJsonObject{
              {QStringLiteral("type"), QStringLiteral("array")},
              {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
          }},
      }},
      {QStringLiteral("required"), QJsonArray{
          QStringLiteral("decision"), QStringLiteral("activity"), QStringLiteral("questions"),
          QStringLiteral("plan"), QStringLiteral("reason"), QStringLiteral("warnings")}},
      {QStringLiteral("additionalProperties"), false},
  };
  const auto system_prompt = agent_workmanship_guidance() + QStringLiteral(
      "Return one JSON object matching the supplied schema. This is read-only conversation planning; you cannot edit, create, import, export, or dispatch tools. Understand the user's ordinary open-ended request using the supplied prompt, current project/asset context, optional inspiration, known preferences, and previous answers. Do not ask the user to choose a task category, tools, model, style system, or technical setup. A short, ordinary prompt is enough to make a plan; never require a rewritten or exact prompt. Default to plan_ready with reasonable editable design choices, including dimensions, layout, palette, and typography. Ask at most two concise questions in one optional clarification round only when the answer materially changes the requested result; never ask for font names, tool choices, exact dimensions, or wording that you can propose. Preserve supplied copy verbatim. For a creative design that needs a headline or caption, propose suitable non-factual wording with source=proposed so the user can review it before Start, unless the user prohibits generated wording. Do not invent dates, named people, event details, addresses, prices, discounts, statistics, or factual claims. Unknown facts can be omitted or shown as clearly labeled editable placeholders such as [Event date], with that choice visible in the plan. When brief.useDefaults or brief.clarificationComplete is true, return plan_ready without more questions: retain supplied answers, make creative choices, and use placeholders for remaining unknown facts. Never ask an already answered question. The plan must state a concise outcome and concrete deliverables; exactCopy contains supplied wording, proposed creative wording, or explicitly labeled placeholders, and may be empty when preserving existing text or when no new wording is needed. Every ready plan must include one to six actions as plain-English result steps, each with kind=color, framing, masking, text, layers, or effects and a concise summary shown to the user. Missing or empty actions cannot be approved. If brief.planValidationFeedback is present, correct that rejected read-only proposal while retaining the original request, exact copy, targets, protections and deliverables. These actions describe the approved outcome, not a tool chain. Include only actions necessary for this request and respect all explicit protections or prohibitions; do not add operations the user has ruled out. For an existing composition revision, retain the original outcome and describe only the approved revision work. For a blank canvas, include canvasSettings with name, whole-pixel width and height (1-8192, at most 32 megapixels), resolution (72-600 pixels per inch), background (transparent, white, or color), and color (CSS color string, empty when unused). Use the requested dimensions when supplied; otherwise propose reasonable editable defaults and include the chosen dimensions in the visible deliverables. This is a planned document only; no creation occurs until Start. Activity must be exactly one allowed short status selected to describe the work currently underway; do not expose reasoning or predictions. If this request cannot be safely described, return cannot_complete with a useful reason.");
  QVariantMap conversation{
      {QStringLiteral("brief"), request.brief},
      {QStringLiteral("context"), request.context},
      {QStringLiteral("answers"), request.answers},
      {QStringLiteral("previousQuestions"), request.prior_questions},
  };
  const auto prompt = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(conversation))
                                            .toJson(QJsonDocument::Compact));
  const auto model_name = model.value(QStringLiteral("chatModel"), model.value(QStringLiteral("model"))).toString().trimmed();
  const auto endpoint = model.value(QStringLiteral("chatEndpoint"), model.value(QStringLiteral("endpoint"))).toString().trimmed();
  if (model_name.isEmpty() || !local_http_endpoint_valid(endpoint)) {
    result.error_message = QStringLiteral("The selected local model has no usable Ollama endpoint.");
    return result;
  }
  auto transport_model = model;
  if (model.value(QStringLiteral("chatContextWindow")).isValid()) {
    transport_model.insert(QStringLiteral("contextWindow"), model.value(QStringLiteral("chatContextWindow")));
  }
  const auto context_tokens = ollama_effective_context_tokens(transport_model, prompt.size(), false);
  // Conversational planning has no inherited generation deadline. A runtime
  // may opt into an explicit chat-only timeout, while the ordinary edit-model
  // request timeout must not silently cut off this session.
  const auto configured_chat_timeout = model.value(QStringLiteral("chatRequestTimeoutMs"));
  const auto chat_timeout = configured_chat_timeout.isValid()
                                ? std::max(0, configured_chat_timeout.toInt())
                                : -1;
  const auto stream_response = static_cast<bool>(request.activity_received);
  const QJsonObject payload{
      {QStringLiteral("model"), model_name},
      {QStringLiteral("stream"), stream_response},
      {QStringLiteral("think"), false},
      {QStringLiteral("keep_alive"), QStringLiteral("10m")},
      {QStringLiteral("format"), schema},
      {QStringLiteral("messages"), QJsonArray{
          QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                      {QStringLiteral("content"), system_prompt}},
          QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                      {QStringLiteral("content"), prompt}},
      }},
      {QStringLiteral("options"), QJsonObject{
          {QStringLiteral("temperature"), 0.1},
          {QStringLiteral("num_predict"), 1200},
          {QStringLiteral("num_ctx"), context_tokens},
      }},
  };
  result.prompt_audit = QVariantMap{
      {QStringLiteral("backend"), QStringLiteral("ollama")},
      {QStringLiteral("modelId"), result.model_id},
      {QStringLiteral("model"), model_name},
      {QStringLiteral("endpoint"), endpoint},
      {QStringLiteral("readOnly"), true},
      {QStringLiteral("toolDispatchAvailable"), false},
      {QStringLiteral("streaming"), true},
      {QStringLiteral("schema"), schema.toVariantMap()},
      {QStringLiteral("inputCharCount"), prompt.size()},
  };
  QString transport_error;
  QVariantMap transport_report;
  const auto response = http_post_json(
      endpoint,
      QStringLiteral("/api/chat"),
      payload,
      chat_timeout,
      &transport_error,
      &transport_report,
      request.cancellation_requested,
      request.activity_received);
  result.prompt_audit.insert(QStringLiteral("transport"), transport_report);
  if (response.isEmpty()) {
    result.error_message = transport_error.isEmpty()
                               ? QStringLiteral("The local model did not return a planning response.")
                               : transport_error;
    return result;
  }
  if (response.value(QStringLiteral("streamed")).toBool() &&
      !response.value(QStringLiteral("done")).toBool()) {
    result.error_message = QStringLiteral("The local model stream ended before its structured response was complete.");
    return result;
  }
  QString response_source;
  const auto content = ollama_structured_response_text(response, &response_source);
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(content.toUtf8(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    result.error_message = QStringLiteral("The local model response was not a complete structured planning object.");
    return result;
  }
  const auto object = document.object();
  const auto allowed_activity = QSet<QString>{
      QStringLiteral("Reviewing the request"),
      QStringLiteral("Checking the selected targets"),
      QStringLiteral("Identifying essential missing details"),
      QStringLiteral("Preparing the requested outcome"),
  };
  const auto reported_activity = object.value(QStringLiteral("activity")).toString().trimmed();
  const auto activity = allowed_activity.contains(reported_activity)
                            ? reported_activity : QStringLiteral("Preparing the requested outcome");
  if (request.activity_received && !response.value(QStringLiteral("streamed")).toBool() &&
      allowed_activity.contains(activity)) {
    // Fake transports and cached test responses do not stream. Still emit only
    // an activity value from a fully parsed, schema-shaped model response.
    request.activity_received(activity);
  }
  result.prompt_audit.insert(QStringLiteral("responseSource"), response_source);
  result.prompt_audit.insert(QStringLiteral("responseCharCount"), content.size());
  result.prompt_audit.insert(QStringLiteral("activity"), activity);
  result.decision = object.value(QStringLiteral("decision")).toString().trimmed();
  if (result.decision != QStringLiteral("ask_questions") &&
      result.decision != QStringLiteral("plan_ready") &&
      result.decision != QStringLiteral("cannot_complete")) {
    result.error_message = QStringLiteral("The local model returned an unsupported planning decision.");
    return result;
  }
  // Cosmetic activity labels and stale optional questions do not invalidate
  // an otherwise usable plan. Target, action and copy validation still apply.
  if (use_defaults && result.decision == QStringLiteral("ask_questions"))
    result.decision = QStringLiteral("plan_ready");
  const auto raw_questions = result.decision == QStringLiteral("ask_questions")
                                ? object.value(QStringLiteral("questions")).toArray() : QJsonArray{};
  if (raw_questions.size() > 2) {
    result.error_message = QStringLiteral("The local model asked more than two questions on one page.");
    return result;
  }
  const auto prior_answer_keys = request.answers.keys();
  QSet<QString> question_ids;
  for (const auto& question_value : raw_questions) {
    const auto question_object = question_value.toObject();
    const auto id = question_object.value(QStringLiteral("id")).toString().trimmed();
    const auto question_prompt = question_object.value(QStringLiteral("prompt")).toString().simplified();
    const auto input_type = question_object.value(QStringLiteral("inputType")).toString();
    const auto options = question_object.value(QStringLiteral("options")).toArray().toVariantList();
    if (prior_answer_keys.contains(id)) continue;
    static const QRegularExpression id_pattern(QStringLiteral("^[a-z][a-z0-9_]{0,47}$"));
    if (!id_pattern.match(id).hasMatch() || question_ids.contains(id) ||
        question_prompt.isEmpty() ||
        question_prompt.size() > 220 ||
        (input_type != QStringLiteral("text") && input_type != QStringLiteral("choice")) ||
        (input_type == QStringLiteral("choice") && (options.size() < 2 || options.size() > 8)) ||
        (input_type == QStringLiteral("text") && !options.isEmpty())) {
      result.error_message = QStringLiteral("The local model returned an invalid or repeated question.");
      return result;
    }
    question_ids.insert(id);
    result.questions.append(QVariantMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("prompt"), question_prompt},
        {QStringLiteral("reason"), question_object.value(QStringLiteral("reason")).toString().simplified()},
        {QStringLiteral("required"), question_object.value(QStringLiteral("required")).toBool()},
        {QStringLiteral("inputType"), input_type},
        {QStringLiteral("options"), options},
    });
  }
  if (result.decision == QStringLiteral("cannot_complete")) {
    result.error_message = object.value(QStringLiteral("reason")).toString().simplified();
    if (result.error_message.isEmpty()) {
      result.error_message = QStringLiteral("The local model could not describe a safe plan for this request.");
    }
    return result;
  }
  if (result.decision == QStringLiteral("ask_questions")) {
    if (result.questions.isEmpty()) {
      result.error_message = QStringLiteral("The local model requested clarification without asking a usable question.");
      return result;
    }
    result.ok = true;
    return result;
  }
  if (!result.questions.isEmpty()) {
    result.error_message = QStringLiteral("The local model returned a ready plan alongside unanswered questions.");
    return result;
  }
  auto plan = object.value(QStringLiteral("plan")).toObject().toVariantMap();
  if (plan.contains(QStringLiteral("canvasSettings")) || request.context.value(QStringLiteral("blankCanvas")).toBool()) {
    const auto settings_value = object.value(QStringLiteral("plan")).toObject().value(QStringLiteral("canvasSettings"));
    const auto settings = settings_value.toObject();
    result.prompt_audit.insert(QStringLiteral("canvasSettingsCandidate"), settings.toVariantMap());
    const auto width = settings.value(QStringLiteral("width")).toDouble();
    const auto height = settings.value(QStringLiteral("height")).toDouble();
    const auto dpi = settings.value(QStringLiteral("resolution")).toDouble();
    const auto background = settings.value(QStringLiteral("background")).toString();
    if (!settings_value.isObject() || settings.size() != 6 ||
        settings.value(QStringLiteral("name")).toString().trimmed().isEmpty() ||
        !settings.value(QStringLiteral("width")).isDouble() || !settings.value(QStringLiteral("height")).isDouble() ||
        width < 1 || height < 1 || width > 8192 || height > 8192 ||
        width != std::floor(width) || height != std::floor(height) || width * height > 32.0 * 1024.0 * 1024.0 ||
        !settings.value(QStringLiteral("resolution")).isDouble() || dpi < 72 || dpi > 600 ||
        !QStringList{"transparent", "white", "color"}.contains(background) ||
        !settings.value(QStringLiteral("color")).isString() ||
        (background == QStringLiteral("color") && !QColor(settings.value(QStringLiteral("color")).toString()).isValid())) {
      result.error_message = QStringLiteral("The blank-canvas plan needs valid dimensions, resolution and background before Start.");
      return result;
    }
  }
  {
    const auto actions_value = object.value(QStringLiteral("plan")).toObject().value(QStringLiteral("actions"));
    static const QStringList kinds{"color", "framing", "masking", "text", "layers", "effects"};
    const auto reject_actions = [&](const QString& error) {
      result.error_message = error;
      QVariantMap feedback{
          {QStringLiteral("code"), QStringLiteral("invalid_typed_actions")},
          {QStringLiteral("instruction"), QStringLiteral(
              "Return one to six typed outcome actions, each containing only kind and summary. "
              "Use color, framing, masking, text, layers or effects only as necessary for the original request. "
              "Preserve the original prompt, copy, protections, targets and deliverables. "
              "This correction is read-only; do not execute tools or infer permissions from prose.")},
      };
      if (QJsonDocument::fromVariant(plan).toJson(QJsonDocument::Compact).size() <= 8192)
        feedback.insert(QStringLiteral("candidatePlan"), plan);
      result.prompt_audit.insert(QStringLiteral("planValidationFailure"), feedback);
    };
    if (!actions_value.isArray() || actions_value.toArray().isEmpty() || actions_value.toArray().size() > 6) {
      reject_actions(QStringLiteral("The plan must contain one to six concise typed outcome actions before Start."));
      return result;
    }
    for (const auto& value : actions_value.toArray()) {
      const auto action = value.toObject();
      const auto summary = action.value(QStringLiteral("summary")).toString().trimmed();
      if (!value.isObject() || action.size() != 2 ||
          !kinds.contains(action.value(QStringLiteral("kind")).toString()) ||
          summary.isEmpty() || summary.size() > 180) {
        reject_actions(QStringLiteral("The plan contains an unsupported or unreadable outcome action."));
        return result;
      }
    }
  }
  const auto summary = plan.value(QStringLiteral("summary")).toString().simplified();
  const auto outcome = plan.value(QStringLiteral("outcome")).toString().simplified();
  const auto deliverables = plan.value(QStringLiteral("deliverables")).toStringList();
  if (summary.isEmpty() || outcome.isEmpty() || deliverables.isEmpty()) {
    result.error_message = QStringLiteral("The local model plan is missing its outcome or deliverables.");
    return result;
  }
  QStringList supplied_request_texts{prompt_text};
  for (const auto& answer_key : request.answers.keys()) {
    const auto answer = request.answers.value(answer_key).toString().trimmed();
    if (!answer.isEmpty()) supplied_request_texts.append(answer);
  }
  const auto request_text = supplied_request_texts.join(QLatin1Char(' ')).toLower();
  static const QRegularExpression copy_suggestion_negation(
      QStringLiteral("\\b(don't|do not|never|no|without|avoid)\\s+(?:new|generated|invented)\\s+(copy|headlines?|titles?|wording|text)\\b|\\b(don't|do not|never|without|avoid)\\s+(?:suggest|write|draft|generate|invent|make up)\\b.{0,48}\\b(copy|headlines?|titles?|wording|text)\\b|\\b(use|keep|preserve)\\s+(?:only\\s+)?(?:the\\s+)?(?:existing|supplied|original|exact)\\s+(copy|headlines?|titles?|wording|text)\\b"),
      QRegularExpression::CaseInsensitiveOption);
  const auto allows_proposed_copy = !copy_suggestion_negation.match(request_text).hasMatch();
  result.prompt_audit.insert(QStringLiteral("creativeCopyAllowed"), allows_proposed_copy);
  result.prompt_audit.insert(QStringLiteral("usingEditableDefaults"), use_defaults);
  QVariantList valid_copy;
  QStringList answer_texts = supplied_request_texts;
  if (!answer_texts.isEmpty()) answer_texts.removeFirst();
  QVariantList contextual_copy;
  for (const auto& key : {QStringLiteral("userProvidedCopy"), QStringLiteral("knownCopy"), QStringLiteral("exactCopy")}) {
    const auto value = request.context.value(key);
    if (value.metaType().id() == QMetaType::QVariantList) contextual_copy.append(value.toList());
    else if (value.metaType().id() == QMetaType::QString) contextual_copy.append(value);
  }
  QStringList context_texts;
  for (const auto& copy_value : contextual_copy) {
    const auto copy_map = copy_value.toMap();
    const auto copy = copy_value.metaType().id() == QMetaType::QString
                          ? copy_value.toString().trimmed()
                          : copy_map.value(QStringLiteral("text")).toString().trimmed();
    if (!copy.isEmpty()) context_texts.append(copy);
  }
  auto unsourced_copy = false;
  auto proposed_copy = false;
  static const QRegularExpression factual_copy_pattern(
      QStringLiteral("\\d|[$€£%]|\\b(free (admission|entry|shipping)|sold out|guaranteed|january|february|march|april|may|june|july|august|september|october|november|december|monday|tuesday|wednesday|thursday|friday|saturday|sunday)\\b"),
      QRegularExpression::CaseInsensitiveOption);
  for (const auto& copy_value : plan.value(QStringLiteral("exactCopy")).toList()) {
    const auto copy_map = copy_value.toMap();
    const auto text = copy_map.value(QStringLiteral("text")).toString().trimmed();
    if (text.isEmpty() || text.size() > 300) {
      result.error_message = QStringLiteral("The local model returned invalid exact copy.");
      return result;
    }
    const auto in_prompt = prompt_text.contains(text, Qt::CaseSensitive);
    const auto in_answer = std::any_of(answer_texts.cbegin(), answer_texts.cend(),
        [&text](const QString& supplied) { return supplied.contains(text, Qt::CaseSensitive); });
    const auto in_context = std::any_of(context_texts.cbegin(), context_texts.cend(),
        [&text](const QString& supplied) { return supplied.contains(text, Qt::CaseSensitive); });
    if (!in_prompt && !in_answer && !in_context) {
      const auto is_placeholder = text.startsWith(QLatin1Char('[')) && text.endsWith(QLatin1Char(']'));
      const auto unsupported_fact = !is_placeholder && factual_copy_pattern.match(text).hasMatch();
      if (allows_proposed_copy && (!unsupported_fact || use_defaults)) {
        proposed_copy = true;
        const auto approved_text = unsupported_fact ? QStringLiteral("[Headline / event details]") : text;
        valid_copy.append(QVariantMap{
            {QStringLiteral("text"), approved_text},
            {QStringLiteral("source"), QStringLiteral("proposed")},
            {QStringLiteral("needsApproval"), true},
        });
        if (unsupported_fact) {
          auto constraints = plan.value(QStringLiteral("constraints")).toStringList();
          constraints.append(QStringLiteral("Keep unknown factual wording as an editable placeholder; do not invent it."));
          plan.insert(QStringLiteral("constraints"), constraints);
        }
        continue;
      }
      unsourced_copy = true;
      continue;
    }
    const auto source_name = in_prompt ? QStringLiteral("prompt")
                            : in_answer ? QStringLiteral("answer")
                                        : QStringLiteral("context");
    valid_copy.append(QVariantMap{
        {QStringLiteral("text"), text},
        {QStringLiteral("source"), source_name},
    });
  }
  plan.insert(QStringLiteral("summary"), summary);
  plan.insert(QStringLiteral("outcome"), outcome);
  plan.insert(QStringLiteral("deliverables"), deliverables);
  plan.insert(QStringLiteral("exactCopy"), valid_copy);
  plan.insert(QStringLiteral("copySuggestionApprovalRequired"), proposed_copy);
  if (unsourced_copy && !use_defaults) {
    QVariantList questions;
    questions.append(QVariantMap{
        {QStringLiteral("id"), QStringLiteral("exact_copy")},
        {QStringLiteral("prompt"), QStringLiteral("What factual wording should appear? Or use my judgment for a draft.")},
        {QStringLiteral("reason"), QStringLiteral("A proposed detail was not supplied. You can provide it or leave an editable placeholder.")},
        {QStringLiteral("required"), true},
        {QStringLiteral("inputType"), QStringLiteral("text")},
        {QStringLiteral("options"), QVariantList{}},
    });
    result.questions = questions;
    result.decision = QStringLiteral("ask_questions");
    result.ok = true;
    return result;
  }
  if (unsourced_copy) {
    auto constraints = plan.value(QStringLiteral("constraints")).toStringList();
    constraints.append(QStringLiteral("Preserve supplied and existing wording; omit any unsupplied copy."));
    plan.insert(QStringLiteral("constraints"), constraints);
  }
  plan.insert(QStringLiteral("briefVersion"), request.brief.value(QStringLiteral("version")));
  result.plan = plan;
  for (const auto& warning : object.value(QStringLiteral("warnings")).toArray()) {
    const auto warning_text = warning.toString().simplified();
    if (!warning_text.isEmpty()) result.warnings.append(warning_text);
  }
  result.ok = true;
  return result;
}

QStringList agent_composition_text_layer_ids(const QVariantList& tool_reports) {
  QStringList ids;
  for (const auto& value : tool_reports) {
    const auto report = value.toMap();
    if (!report.value(QStringLiteral("ok")).toBool()) continue;
    const auto name = report.value(QStringLiteral("toolName")).toString();
    if (name != QStringLiteral("photo.edit_composition") && name != QStringLiteral("app.invoke_command")) continue;
    const auto arguments = report.value(QStringLiteral("arguments")).toMap();
    const auto native_command = arguments.value(QStringLiteral("commandId")).toString();
    if (name == QStringLiteral("app.invoke_command") && !native_command.contains(QStringLiteral("Text")) &&
        !native_command.contains(QStringLiteral("Typography"))) continue;
    const auto id = arguments.value(QStringLiteral("operation")).toString() == QStringLiteral("add_text") ||
        (name == QStringLiteral("app.invoke_command") && native_command.contains(QStringLiteral(".add")))
        ? report.value(QStringLiteral("structuredContent")).toMap().value(QStringLiteral("createdLayerId")).toString()
        : (name == QStringLiteral("app.invoke_command") ? arguments.value(QStringLiteral("values")).toMap() : arguments)
              .value(QStringLiteral("layerId")).toString();
    if (!id.isEmpty() && !ids.contains(id)) ids.append(id);
  }
  return ids;
}

QVariantMap agent_graphic_proof_layout_metrics(const QVariantMap& layer) {
  bool scale_ok = true;
  const auto raw_scale = layer.contains(QStringLiteral("scale"))
      ? layer.value(QStringLiteral("scale")).toDouble(&scale_ok)
      : 1.0;
  bool rotation_ok = true;
  const auto raw_rotation = layer.value(QStringLiteral("rotation"), 0.0).toDouble(&rotation_ok);
  bool x_ok = false;
  bool y_ok = false;
  bool width_ok = false;
  bool height_ok = false;
  bool canvas_width_ok = false;
  bool canvas_height_ok = false;
  const auto x = layer.value(QStringLiteral("x")).toDouble(&x_ok);
  const auto y = layer.value(QStringLiteral("y")).toDouble(&y_ok);
  const auto width = layer.value(QStringLiteral("pixelWidth")).toDouble(&width_ok);
  const auto height = layer.value(QStringLiteral("pixelHeight")).toDouble(&height_ok);
  const auto canvas_width = layer.value(QStringLiteral("canvasWidth")).toDouble(&canvas_width_ok);
  const auto canvas_height = layer.value(QStringLiteral("canvasHeight")).toDouble(&canvas_height_ok);
  const auto valid_transform = scale_ok && rotation_ok && std::isfinite(raw_scale) &&
      raw_scale > 0.0 && std::isfinite(raw_rotation);
  const auto valid_geometry = x_ok && y_ok && width_ok && height_ok &&
      canvas_width_ok && canvas_height_ok && std::isfinite(x) && std::isfinite(y) &&
      std::isfinite(width) && std::isfinite(height) && std::isfinite(canvas_width) &&
      std::isfinite(canvas_height) && width > 0.0 && height > 0.0 &&
      canvas_width > 0.0 && canvas_height > 0.0;
  if (!valid_transform || !valid_geometry) {
    const auto error = !valid_transform
        ? QStringLiteral("Graphic layer scale or rotation is invalid.")
        : QStringLiteral("Graphic layer bounds or canvas dimensions are invalid.");
    return QVariantMap{
        {QStringLiteral("available"), false},
        {QStringLiteral("transformAvailable"), valid_transform},
        {QStringLiteral("geometryAvailable"), valid_geometry},
        {QStringLiteral("error"), error},
    };
  }

  // Photo layer transforms keep x/y anchored at the unscaled layer origin.
  // Rotation occurs around the center of the scaled raster.
  const auto scale = std::clamp(raw_scale, 0.05, 32.0);
  const auto rotation = std::clamp(raw_rotation, -3600.0, 3600.0);
  const auto scaled_width = width * scale;
  const auto scaled_height = height * scale;
  const auto radians = rotation * std::acos(-1.0) / 180.0;
  const auto cosine = std::abs(std::cos(radians));
  const auto sine = std::abs(std::sin(radians));
  const auto rendered_width = scaled_width * cosine + scaled_height * sine;
  const auto rendered_height = scaled_width * sine + scaled_height * cosine;
  const auto center_x = x + scaled_width * 0.5;
  const auto center_y = y + scaled_height * 0.5;
  const auto rendered_x = center_x - rendered_width * 0.5;
  const auto rendered_y = center_y - rendered_height * 0.5;
  const auto finite_bounds = std::isfinite(rendered_x) && std::isfinite(rendered_y) &&
      std::isfinite(rendered_width) && std::isfinite(rendered_height) &&
      rendered_width > 0.0 && rendered_height > 0.0;
  if (!finite_bounds) {
    return QVariantMap{
        {QStringLiteral("available"), false},
        {QStringLiteral("transformAvailable"), true},
        {QStringLiteral("geometryAvailable"), true},
        {QStringLiteral("error"), QStringLiteral("Transformed graphic layer bounds overflowed.")},
    };
  }

  const auto within_canvas = rendered_x >= 0.0 && rendered_y >= 0.0 &&
      rendered_x + rendered_width <= canvas_width &&
      rendered_y + rendered_height <= canvas_height;
  QVariantMap result{
      {QStringLiteral("available"), true},
      {QStringLiteral("transformAvailable"), true},
      {QStringLiteral("geometryAvailable"), true},
      {QStringLiteral("boundsBasis"), QStringLiteral("declared-layer-raster")},
      {QStringLiteral("visibleInkBoundsAvailable"), false},
      {QStringLiteral("effectPaddingIncluded"), false},
      {QStringLiteral("rasterFrameWithinCanvas"), within_canvas},
      {QStringLiteral("scale"), scale},
      {QStringLiteral("rotation"), rotation},
      {QStringLiteral("canvasWidth"), canvas_width},
      {QStringLiteral("canvasHeight"), canvas_height},
      {QStringLiteral("unscaledBounds"), QVariantMap{
          {QStringLiteral("x"), x}, {QStringLiteral("y"), y},
          {QStringLiteral("width"), width}, {QStringLiteral("height"), height}}},
      {QStringLiteral("renderedBounds"), QVariantMap{
          {QStringLiteral("x"), rendered_x}, {QStringLiteral("y"), rendered_y},
          {QStringLiteral("width"), rendered_width}, {QStringLiteral("height"), rendered_height}}},
  };
  const auto typography = layer.value(QStringLiteral("textTypography")).toMap();
  if (typography.contains(QStringLiteral("size"))) {
    bool typography_size_ok = false;
    const auto typography_size = typography.value(QStringLiteral("size")).toDouble(&typography_size_ok);
    const auto effective_size = typography_size * scale;
    const auto effective_size_available = typography_size_ok &&
        std::isfinite(typography_size) && typography_size > 0.0 &&
        std::isfinite(effective_size) && effective_size > 0.0;
    result.insert(QStringLiteral("typographySizeAvailable"), effective_size_available);
    if (effective_size_available) {
      result.insert(QStringLiteral("typographySize"), typography_size);
      result.insert(QStringLiteral("effectiveTypographySize"), effective_size);
    }
  }
  return result;
}

QVariantList agent_graphic_proof_layers(
    const QVariantList& layers, const QStringList& composition_text_layer_ids) {
  QVariantList result;
  for (qsizetype index = 0; index < layers.size(); ++index) {
    const auto layer = layers.at(index).toMap();
    if (layer.value(QStringLiteral("kind")).toString() != QStringLiteral("text") ||
        (!layer.value(QStringLiteral("agentGraphic")).toBool() &&
         !composition_text_layer_ids.contains(layer.value(QStringLiteral("id")).toString()))) continue;
    QVariantMap metadata;
    for (const auto& key : {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("text"),
             QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("pixelWidth"), QStringLiteral("pixelHeight"),
             QStringLiteral("canvasWidth"), QStringLiteral("canvasHeight"), QStringLiteral("scale"),
             QStringLiteral("rotation"), QStringLiteral("textTypography")})
      if (layer.contains(key)) metadata.insert(key, layer.value(key));
    const auto layout_metrics = agent_graphic_proof_layout_metrics(layer);
    if (!metadata.contains(QStringLiteral("scale")))
      metadata.insert(QStringLiteral("scale"), layout_metrics.value(QStringLiteral("scale"), 1.0));
    if (!metadata.contains(QStringLiteral("rotation")))
      metadata.insert(QStringLiteral("rotation"), layout_metrics.value(QStringLiteral("rotation"), 0.0));
    metadata.insert(QStringLiteral("layoutMetrics"), layout_metrics);
    if (layout_metrics.contains(QStringLiteral("effectiveTypographySize")))
      metadata.insert(QStringLiteral("effectiveTypographySize"),
                       layout_metrics.value(QStringLiteral("effectiveTypographySize")));
    if (layout_metrics.contains(QStringLiteral("renderedBounds")))
      metadata.insert(QStringLiteral("renderedBounds"),
                       layout_metrics.value(QStringLiteral("renderedBounds")));
    metadata.insert(QStringLiteral("position"), index);
    metadata.insert(QStringLiteral("order"), QStringLiteral("top-first"));
    result.append(metadata);
  }
  return result;
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
  QStringList expected_graphic_layer_ids;
  QSet<QString> expected_graphic_layer_id_set;
  for (const auto& layer_value : request.graphic_layers) {
    const auto layer_id = layer_value.toMap().value(QStringLiteral("id")).toString().trimmed();
    if (layer_id.isEmpty() || expected_graphic_layer_id_set.contains(layer_id)) {
      result.error_message = QStringLiteral(
          "Every required graphic text layer must have a distinct stable identity for its detail proof.");
      return result;
    }
    expected_graphic_layer_id_set.insert(layer_id);
    expected_graphic_layer_ids.append(layer_id);
  }

  const auto requested_overview_path = request.asset.value(QStringLiteral("proofImagePath")).toString();
  const auto requested_detail_paths = request.asset
      .value(QStringLiteral("additionalProofImagePaths")).toStringList();
  const auto composition_review = request.validation_report
      .value(QStringLiteral("compositionReview")).toMap();
  const auto detail_report = composition_review.value(QStringLiteral("detailProofReport")).toMap();
  const auto reported_overview_path = detail_report.value(QStringLiteral("overviewPath")).toString();
  const auto reported_overview_sha = detail_report.value(QStringLiteral("overviewSha256")).toString();
  const auto requested_overview_sha = request.asset.value(QStringLiteral("planningProofSha256")).toString();
  if (composition_review.contains(QStringLiteral("detailProofImagePaths")) &&
      composition_review.value(QStringLiteral("detailProofImagePaths")).toStringList() != requested_detail_paths) {
    result.error_message = QStringLiteral(
        "The composition review detail paths do not match the images attached for critique.");
    return result;
  }
  if (!reported_overview_path.isEmpty() && reported_overview_path != requested_overview_path) {
    result.error_message = QStringLiteral(
        "The reported overview proof path does not match the image attached for critique.");
    return result;
  }
  if (!reported_overview_sha.isEmpty() && reported_overview_sha != requested_overview_sha) {
    result.error_message = QStringLiteral(
        "The reported overview proof hash does not match the image attached for critique.");
    return result;
  }
  if (detail_report.contains(QStringLiteral("detailProofImagePaths")) &&
      detail_report.value(QStringLiteral("detailProofImagePaths")).toStringList() != requested_detail_paths) {
    result.error_message = QStringLiteral(
        "Reported targeted detail paths do not match the images attached for critique.");
    return result;
  }
  const auto reported_detail_rows = detail_report.value(QStringLiteral("targetedDetailProofs")).toList();
  if (composition_review.contains(QStringLiteral("targetedDetailProofs")) &&
      composition_review.value(QStringLiteral("targetedDetailProofs")).toList() != reported_detail_rows) {
    result.error_message = QStringLiteral(
        "The composition review targeted detail rows do not match their canonical proof report.");
    return result;
  }
  const auto asset_visual_evidence = request.asset.value(QStringLiteral("visualEvidence")).toMap();
  const auto asset_detail_rows = asset_visual_evidence.value(QStringLiteral("targetedDetailProofs")).toList();
  const auto report_detail_bindings_complete = request.graphic_layers.isEmpty() ||
      (!reported_overview_path.isEmpty() && !reported_overview_sha.isEmpty() &&
       requested_detail_paths.size() == expected_graphic_layer_ids.size() &&
       detail_report.contains(QStringLiteral("detailProofImagePaths")) &&
       detail_report.value(QStringLiteral("detailProofImagePaths")).toStringList() == requested_detail_paths &&
       reported_detail_rows.size() == expected_graphic_layer_ids.size() &&
       asset_detail_rows.size() == expected_graphic_layer_ids.size());
  if (!reported_detail_rows.isEmpty()) {
    if (reported_detail_rows.size() != requested_detail_paths.size()) {
      result.error_message = QStringLiteral(
          "Reported targeted detail rows do not match the attached proof image count.");
      return result;
    }
    for (qsizetype index = 0; index < reported_detail_rows.size(); ++index) {
      const auto reported_row = reported_detail_rows.at(index).toMap();
      const auto asset_row = index < asset_detail_rows.size()
          ? asset_detail_rows.at(index).toMap() : QVariantMap{};
      bool conflicting_report_metadata = false;
      for (const auto& key : {QStringLiteral("sourceOverviewSha256"),
               QStringLiteral("sourceRecipeSha256"), QStringLiteral("sourceRenderedSha256"),
               QStringLiteral("pixelBounds"), QStringLiteral("renderedBounds"),
               QStringLiteral("fullResolutionSourceSize"), QStringLiteral("boundsBasis"),
               QStringLiteral("available"), QStringLiteral("omitted"),
               QStringLiteral("visibleInkBoundsAvailable"), QStringLiteral("effectPaddingIncluded")}) {
        if (reported_row.contains(key) && asset_row.contains(key) &&
            reported_row.value(key) != asset_row.value(key)) {
          conflicting_report_metadata = true;
          break;
        }
      }
      if (conflicting_report_metadata ||
          reported_row.value(QStringLiteral("imagePath")).toString() != requested_detail_paths.at(index) ||
          (!expected_graphic_layer_ids.isEmpty() &&
           (reported_row.value(QStringLiteral("layerId")).toString() != expected_graphic_layer_ids.at(index) ||
            asset_row.value(QStringLiteral("layerId")).toString() != expected_graphic_layer_ids.at(index) ||
            reported_row.value(QStringLiteral("imageSha256")) != asset_row.value(QStringLiteral("imageSha256")) ||
            reported_row.value(QStringLiteral("imageSha256")).toString().isEmpty()))) {
        result.error_message = QStringLiteral(
            "A reported targeted detail row does not match its attached path, layer identity, or image hash.");
        return result;
      }
    }
  }

  const auto endpoint = model.value(QStringLiteral("endpoint")).toString();
  const auto model_name = model.value(QStringLiteral("model")).toString();
  auto prompt = ollama_proof_critique_prompt(request);
  if (!report_detail_bindings_complete) {
    prompt += QStringLiteral(
        "\nThe report omitted complete overview-to-crop metadata bindings. The validated attached asset paths remain authoritative; do not infer missing crop identities or claim crop-specific metadata verification. Request human review.");
  }
  // Expected binary-mask omission is not missing visual evidence: the rendered
  // after image is required separately. Truncated qualitative/validation
  // metadata, however, must not be waived by an optimistic model response.
  const auto metadata_evidence_complete =
      !prompt.contains(QStringLiteral("\"evidenceOmittedForSize\":true")) &&
      !prompt.contains(QStringLiteral("[overlong text omitted:")) &&
      !prompt.contains(QStringLiteral("[nested evidence omitted]")) &&
      !prompt.contains(QStringLiteral("[additional entries omitted]")) &&
      report_detail_bindings_complete;
  const auto scoped_unchanged_photo_graphic_review =
      !request.graphic_layers.isEmpty() &&
      has_verified_unchanged_photo_baseline(request.validation_report, request.asset);
  const auto system_prompt = (scoped_unchanged_photo_graphic_review
      ? QStringLiteral(
          "Workmanship contract (looksy-workmanship-v1): read-only proof review; do not mutate files or propose recipe data. Return JSON with pass, summary, issues, repairHints, needsUserReview, tasteConfidence and optional warnings. If evidence is missing or omitted, request human review.")
      : QStringLiteral(
          "Workmanship contract (looksy-workmanship-v1): read-only rendered-proof review; never mutate or return recipe data. Judge visible pixels against the approved brief, current request, active style and deterministic validation; successful edits and metadata do not prove quality. Review complete copy, intended-size readability, contrast, hierarchy, clipping, margins, collisions and focal clearance. Use layoutMetrics.renderedBounds for transformed frame fit and effectiveTypographySize for requested type-size ratios; neither describes visible glyph edges/effects, which must be judged from overview/detail pixels. Missing, omitted or insufficient evidence requires human review. Return compact JSON with pass, summary, issues, repairHints, needsUserReview, tasteConfidence and optional warnings. Image 0 is the required overview; later images are supplemental targeted crops bound to the overview and layer."));
  QJsonObject user_message{
      {QStringLiteral("role"), QStringLiteral("user")},
      {QStringLiteral("content"), prompt},
  };
  auto proof_asset = request.asset;
  proof_asset.remove(QStringLiteral("filePath"));
  proof_asset.remove(QStringLiteral("beforeProofPath"));
  proof_asset.remove(QStringLiteral("currentProofPath"));
  const auto image_payload = image_payload_for_asset(proof_asset, ollama_max_image_edge(model));
  if (image_payload.base64.isEmpty()) {
    result.error_message = QStringLiteral("The rendered after proof is unavailable. The original image was not substituted for review.");
    return result;
  }
  const auto additional_images = additional_proof_images_for_asset(
      proof_asset, ollama_max_image_edge(model), !request.graphic_layers.isEmpty(),
      expected_graphic_layer_ids);
  if (!additional_images.valid) {
    result.error_message = additional_images.error_message;
    return result;
  }
  const auto image_included = !image_payload.base64.isEmpty();
  constexpr int kProofCritiqueMaxOutputTokens = 512;
  const auto attached_image_count = image_included
      ? 1 + additional_images.payloads.size() : 0;
  QList<QSize> attached_image_sizes;
  if (image_included) {
    attached_image_sizes.append(image_payload.pixel_size);
    for (const auto& detail : additional_images.payloads)
      attached_image_sizes.append(detail.pixel_size);
  }
  const auto system_prompt_utf8_bytes = static_cast<qint64>(system_prompt.toUtf8().size());
  const auto user_prompt_utf8_bytes = static_cast<qint64>(prompt.toUtf8().size());
  const auto aggregate_text_utf8_bytes = system_prompt_utf8_bytes + user_prompt_utf8_bytes;
  int estimated_aggregate_text_tokens = 0;
  qint64 estimated_ascii_utf8_bytes = 0;
  qint64 estimated_non_ascii_utf8_bytes = 0;
  qint64 estimated_non_ascii_code_points = 0;
  QVariantList image_context_reservations;
  const auto estimated_context_tokens_before_cap = ollama_planning_context_requirement(
      system_prompt, prompt, model_name, attached_image_sizes,
      static_cast<int>(attached_image_count),
      kProofCritiqueMaxOutputTokens, &estimated_aggregate_text_tokens,
      &estimated_ascii_utf8_bytes, &estimated_non_ascii_utf8_bytes,
      &estimated_non_ascii_code_points, &image_context_reservations);
  const auto context_token_limit = std::min(ollama_context_tokens(model), 24576);
  const auto context_tokens = context_token_limit;
  const auto response_schema = ollama_proof_critique_payload_schema();
  const auto response_schema_utf8_bytes = QJsonDocument(response_schema)
      .toJson(QJsonDocument::Compact).size();
  qint64 additional_proof_image_bytes = 0;
  for (const auto& detail : additional_images.payloads)
    additional_proof_image_bytes += detail.transmitted_bytes;
  const QVariantMap prompt_component_audit{
      {QStringLiteral("contract"), QStringLiteral("ollama-proof-critique-prompt-components-v1")},
      {QStringLiteral("systemPromptUtf8Bytes"), system_prompt_utf8_bytes},
      {QStringLiteral("userPromptUtf8Bytes"), user_prompt_utf8_bytes},
      {QStringLiteral("aggregateTextUtf8Bytes"), aggregate_text_utf8_bytes},
      {QStringLiteral("attachedImageCount"), attached_image_count},
      {QStringLiteral("primaryImageTransmittedBytes"), image_included
           ? image_payload.transmitted_bytes : 0},
      {QStringLiteral("additionalProofImageCount"), additional_images.payloads.size()},
      {QStringLiteral("additionalProofImageTransmittedBytes"), additional_proof_image_bytes},
      {QStringLiteral("imageContextReservations"), image_context_reservations},
      {QStringLiteral("maxOutputTokens"), kProofCritiqueMaxOutputTokens},
      {QStringLiteral("responseSchemaUtf8Bytes"), response_schema_utf8_bytes},
      {QStringLiteral("contextTokensRequested"), context_tokens},
      {QStringLiteral("contextTokenLimit"), context_token_limit},
      {QStringLiteral("estimatedAggregateTextTokens"), estimated_aggregate_text_tokens},
      {QStringLiteral("estimatedAsciiUtf8Bytes"), estimated_ascii_utf8_bytes},
      {QStringLiteral("estimatedNonAsciiUtf8Bytes"), estimated_non_ascii_utf8_bytes},
      {QStringLiteral("estimatedNonAsciiCodePointCount"), estimated_non_ascii_code_points},
      {QStringLiteral("estimatedContextTokensBeforeCap"), estimated_context_tokens_before_cap},
      {QStringLiteral("contextEstimateMethod"),
       QStringLiteral("ceil(ascii_utf8_bytes / 4) + non_ascii_utf8_bytes + 512 + output_tokens + sum(imageContextReservations); Qwen3.5/Qwen3.6 valid transmitted dimensions <=1600px per edge use max(2048, ceil512(ceil(w/32)*ceil(h/32)+512)); other recognized Qwen vision images <=768px per edge reserve 2048; all other images reserve 4096; conservative processor/dimension proxy, not exact Ollama tokenization; non-ASCII is charged by UTF-8 byte fallback; format grammar audited separately")},
  };
  if (image_included) {
    QJsonArray images{image_payload.base64};
    for (const auto& detail : additional_images.payloads) images.append(detail.base64);
    user_message.insert(QStringLiteral("images"), images);
  }
  const QJsonObject payload{
      {QStringLiteral("model"), model_name},
      {QStringLiteral("stream"), false},
      {QStringLiteral("think"), false},
      {QStringLiteral("keep_alive"), QStringLiteral("10m")},
      {QStringLiteral("format"), response_schema},
      {QStringLiteral("messages"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("role"), QStringLiteral("system")},
               {QStringLiteral("content"), system_prompt},
           },
           user_message,
       }},
      {QStringLiteral("options"),
       QJsonObject{
           {QStringLiteral("temperature"), 0.0},
           {QStringLiteral("num_predict"), 512},
           {QStringLiteral("num_ctx"), context_tokens},
       }},
  };
  result.prompt_audit = QVariantMap{
      {QStringLiteral("backend"), QStringLiteral("ollama")},
      {QStringLiteral("endpoint"), endpoint},
      {QStringLiteral("model"), model_name},
      {QStringLiteral("schemaMode"), QStringLiteral("proof-critique-json-schema")},
      {QStringLiteral("metadataEvidenceComplete"), metadata_evidence_complete},
      {QStringLiteral("scopedUnchangedPhotoGraphicReview"), scoped_unchanged_photo_graphic_review},
      {QStringLiteral("photoBaselineScopeAudit"), [&request, scoped_unchanged_photo_graphic_review]() {
        const auto evidence = request.validation_report.value(QStringLiteral("photographicQualityEvidence")).toMap();
        QVariantMap audit{{QStringLiteral("verifiedUnchangedPhotoScope"), scoped_unchanged_photo_graphic_review}};
        for (const auto& key : {QStringLiteral("contract"), QStringLiteral("basis"), QStringLiteral("assetId"),
                 QStringLiteral("baselineSourceRecipeSha256"), QStringLiteral("baselineProofSha256"),
                 QStringLiteral("baselineTaskStateHash"), QStringLiteral("normalizedPhotoCompositionSha256"),
                 QStringLiteral("photoRecipeUnchanged"), QStringLiteral("compositePhotoMetricsChangedOnlyByApprovedText"),
                 QStringLiteral("explicitPhotoQualityScope")})
          if (evidence.contains(key)) audit.insert(key, evidence.value(key));
        return audit;
      }()},
      {QStringLiteral("reportDetailBindingsComplete"), report_detail_bindings_complete},
      {QStringLiteral("promptComponents"), prompt_component_audit},
      {QStringLiteral("requestSent"), false},
      {QStringLiteral("deterministicPlanning"), true},
      {QStringLiteral("temperature"), 0.0},
      {QStringLiteral("contextTokens"), context_tokens},
      {QStringLiteral("systemPrompt"), system_prompt},
      {QStringLiteral("userPrompt"), prompt},
      {QStringLiteral("userPromptCharCount"), prompt.size()},
      {QStringLiteral("previousAttemptCount"), request.previous_attempts.size()},
      {QStringLiteral("photographerCalibrationAvailable"),
       request.photographer_calibration
           .value(QStringLiteral("available"))
           .toBool()},
      {QStringLiteral("photographerCalibrationDigest"),
       request.photographer_calibration
           .value(QStringLiteral("profileDigestSha256"))},
      {QStringLiteral("projectNorthStarAvailable"),
       !request.project_north_star.isEmpty()},
      {QStringLiteral("projectNorthStarDigest"),
       request.project_north_star.value(QStringLiteral("digestSha256"))},
      {QStringLiteral("projectNorthStarCurrentRequestWins"), true},
      {QStringLiteral("imageIncluded"), image_included},
      {QStringLiteral("imageByteCount"), image_payload.transmitted_bytes},
      {QStringLiteral("imageOriginalByteCount"), image_payload.original_bytes},
      {QStringLiteral("imageWidth"), image_payload.pixel_size.width()},
      {QStringLiteral("imageHeight"), image_payload.pixel_size.height()},
      {QStringLiteral("imageMaxEdge"), ollama_max_image_edge(model)},
      {QStringLiteral("imageSourceKind"), image_payload.source_kind},
      {QStringLiteral("imageSourcePath"), image_payload.source_path},
      {QStringLiteral("additionalProofImageCount"), additional_images.payloads.size()},
      {QStringLiteral("additionalProofImagePaths"), additional_images.paths},
      {QStringLiteral("responseSchema"), response_schema.toVariantMap()},
  };
  if (!image_included) {
    result.error_message = QStringLiteral("No rendered proof image was available for model critique.");
    result.prompt_audit.insert(QStringLiteral("errorCode"), QStringLiteral("proof_critique_image_unavailable"));
    return result;
  }
  if (estimated_context_tokens_before_cap > context_token_limit) {
    result.error_message = QStringLiteral(
        "The complete rendered proof critique exceeds the selected model's context budget. Preserve all visual evidence and reduce optional review metadata before retrying.");
    result.prompt_audit.insert(QStringLiteral("errorCode"),
        QStringLiteral("proof_critique_context_budget_exceeded"));
    return result;
  }

  result.prompt_audit.insert(QStringLiteral("requestSent"), true);
  QString error_message;
  QVariantMap transport_report;
  const auto response = ollama_frozen_generation_with_output_recovery(
      endpoint, payload, ollama_request_timeout_ms(model),
      estimated_context_tokens_before_cap, context_token_limit,
      request.cancellation_requested, &error_message, &transport_report, &result.prompt_audit);
  result.prompt_audit.insert(QStringLiteral("transport"), transport_report);
  if (response.isEmpty()) {
    result.error_message = error_message.isEmpty()
                               ? QStringLiteral("Ollama proof critique did not return a response.")
                               : error_message;
    if (transport_report.value(QStringLiteral("errorCode")).toString() == QStringLiteral("response_timeout")) {
      result.error_message = QStringLiteral("The local model timed out reviewing the rendered draft. AI verification is incomplete; review the result yourself or retry review.");
    }
    return result;
  }
  // Preserve missing timing fields as missing, not synthetic zero timings.
  // The API reports durations in nanoseconds, including prompt prefill and
  // generation separately. Cached prompt tokens may be absent on older servers.
  QVariantMap server_metrics;
  for (const auto& field : {"total_duration", "load_duration", "prompt_eval_duration", "eval_duration"}) {
    const auto value = response.value(QLatin1String(field));
    const auto duration = value.toDouble(-1.0);
    if (value.isDouble() && std::isfinite(duration) && duration >= 0.0 && duration < 1.0e18)
      server_metrics.insert(QLatin1String(field), qRound64(duration / 1000000.0));
  }
  const QStringList duration_keys{
      QStringLiteral("total_duration"), QStringLiteral("load_duration"),
      QStringLiteral("prompt_eval_duration"), QStringLiteral("eval_duration")};
  const QStringList duration_names{
      QStringLiteral("totalDurationMs"), QStringLiteral("loadDurationMs"),
      QStringLiteral("promptEvalDurationMs"), QStringLiteral("evalDurationMs")};
  for (qsizetype i = 0; i < duration_keys.size(); ++i) {
    if (server_metrics.contains(duration_keys[i]))
      server_metrics.insert(duration_names[i], server_metrics.take(duration_keys[i]));
  }
  const QStringList token_keys{
      QStringLiteral("prompt_eval_count"), QStringLiteral("prompt_eval_cached_count"),
      QStringLiteral("eval_count")};
  const QStringList token_names{
      QStringLiteral("promptEvalCount"), QStringLiteral("promptEvalCachedCount"),
      QStringLiteral("evalCount")};
  for (qsizetype i = 0; i < token_keys.size(); ++i) {
    const auto value = response.value(token_keys[i]);
    if (value.isDouble() && value.toDouble(-1.0) >= 0.0)
      server_metrics.insert(token_names[i], value.toVariant());
  }
  server_metrics.insert(QStringLiteral("available"), !server_metrics.isEmpty());
  result.prompt_audit.insert(QStringLiteral("ollamaServer"), server_metrics);
  result.prompt_audit.insert(QStringLiteral("responseDone"), response.value(QStringLiteral("done")).toVariant());
  result.prompt_audit.insert(QStringLiteral("responseDoneReason"), response.value(QStringLiteral("done_reason")).toVariant());
  if (response.value(QStringLiteral("done")) != QJsonValue(true) ||
      response.value(QStringLiteral("done_reason")).toString() == QStringLiteral("length")) {
    result.error_message = QStringLiteral("The local model did not finish the proof review. AI verification is incomplete.");
    return result;
  }
  QString response_source;
  const auto content = ollama_structured_response_text(response, &response_source);
  result.prompt_audit.insert(QStringLiteral("ollamaResponseSource"), response_source);
  result.prompt_audit.insert(QStringLiteral("responseCharCount"), content.size());
  QJsonParseError parse_error;
  const auto content_document = QJsonDocument::fromJson(content.toUtf8(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !content_document.isObject()) {
    result.error_message = QStringLiteral("Ollama proof critique content was not valid JSON.");
    return result;
  }
  const auto content_object = content_document.object();
  const auto string_array_valid = [](const QJsonValue& value) {
    if (!value.isArray()) return false;
    const auto array = value.toArray();
    return std::all_of(array.cbegin(), array.cend(), [](const QJsonValue& item) {
      return item.isString() && !item.toString().trimmed().isEmpty();
    });
  };
  if (!content_object.value(QStringLiteral("pass")).isBool() ||
      !content_object.value(QStringLiteral("needsUserReview")).isBool() ||
      !content_object.value(QStringLiteral("summary")).isString() ||
      content_object.value(QStringLiteral("summary")).toString().trimmed().isEmpty() ||
      !content_object.value(QStringLiteral("tasteConfidence")).isString() ||
      content_object.value(QStringLiteral("tasteConfidence")).toString().trimmed().isEmpty() ||
      !string_array_valid(content_object.value(QStringLiteral("issues"))) ||
      !string_array_valid(content_object.value(QStringLiteral("repairHints"))) ||
      (content_object.contains(QStringLiteral("warnings")) &&
       !string_array_valid(content_object.value(QStringLiteral("warnings"))))) {
    result.error_message = QStringLiteral("The local model returned an invalid proof-review contract. AI verification is incomplete.");
    return result;
  }
  result.pass = content_object.value(QStringLiteral("pass")).toBool(false);
  result.summary = content_object.value(QStringLiteral("summary")).toString();
  result.issues = content_object.value(QStringLiteral("issues")).toArray().toVariantList();
  result.repair_hints = content_object.value(QStringLiteral("repairHints")).toArray().toVariantList();
  // A model cannot waive unresolved deterministic checks or its own issues.
  if (!request.validation_report.value(QStringLiteral("ok")).toBool() ||
      !metadata_evidence_complete || !result.issues.isEmpty()) result.pass = false;
  result.needs_user_review = !result.pass ||
      request.validation_report.value(QStringLiteral("needsUserReview")).toBool() ||
      content_object.value(QStringLiteral("needsUserReview")).toBool() ||
      !result.issues.isEmpty() || !result.repair_hints.isEmpty() ||
      !content_object.value(QStringLiteral("warnings")).toArray().isEmpty();
  result.taste_confidence = content_object.value(QStringLiteral("tasteConfidence")).toString();
  const auto warning_values = content_object.value(QStringLiteral("warnings")).toArray();
  for (const auto& warning : warning_values) {
    result.warnings.append(warning.toString());
  }
  result.ok = true;
  return result;
}

bool agent_instructions_require_grounded_edit(const QString& instructions) {
  const auto direction = instructions.toLower();
  for (const auto& term : {"background", "subject", "mask", "unchanged", "retouch", "remove", "sky"}) {
    if (direction.contains(QLatin1String(term))) return true;
  }
  return false;
}

// A complete, deliberately small command grammar, not keyword extraction from
// an arbitrary instruction. Unrecognized clauses must go through the model.
static AgentLocalModelPlanResult semantic_command_plan(const AgentLocalModelPlanRequest& request) {
  AgentLocalModelPlanResult result;
  if (request.force_model_planning || !style_tool_enabled(request.style, QStringLiteral("masks"))) return result;
  static const QRegularExpression grammar(QStringLiteral(
      "^(?:please )?(warm|cool|brighten|darken|desaturate) (?:only )?(?:the )?(?:entire |whole )?"
      "(background|subject|sky)(?:[,\\.]? (?:and )?(?:keep|leave) (?:the )?"
      "(people|person|subject|rest(?: of (?:the )?(?:image|photo))?) (?:unchanged|untouched))?[.!]?$"),
      QRegularExpression::CaseInsensitiveOption);
  const auto match = grammar.match(request.user_instructions.simplified());
  if (!match.hasMatch()) return result;
  const auto operation = match.captured(1).toLower();
  const auto target = match.captured(2).toLower();
  const auto protected_target = match.captured(3).toLower();
  if (target == QStringLiteral("subject") && !protected_target.isEmpty() && !protected_target.startsWith(QStringLiteral("rest"))) return result;
  QVariantMap adjustments;
  if (operation == QStringLiteral("warm") || operation == QStringLiteral("cool"))
    adjustments.insert(QStringLiteral("temperature"), operation == QStringLiteral("warm") ? 12.0 : -12.0);
  else if (operation == QStringLiteral("brighten") || operation == QStringLiteral("darken"))
    adjustments.insert(QStringLiteral("exposure"), operation == QStringLiteral("brighten") ? 0.25 : -0.25);
  else adjustments.insert(QStringLiteral("saturation"), -12.0);
  const QVariantMap action{{"target", target}, {"targetLabel", target}, {"selectionMode", "semantic"},
      {"maskShape", "radial"}, {"reason", "Apply the explicit command only through a generated semantic mask."},
      {"confidence", 1.0}, {"region", QVariantMap{{"centerX", 0.5}, {"centerY", 0.5},
          {"radiusX", 0.5}, {"radiusY", 0.5}, {"feather", 0.0}}}, {"adjustments", adjustments}};
  result.model_id = request.model_id;
  result.decision = QStringLiteral("recipe_patch");
  result.plan_summary = QStringLiteral("Prepare a gentle %1 adjustment on the %2 only, using a generated mask. Global adjustments stay unchanged.").arg(operation, target);
  result.style_constraint_citations = cited_style_constraints(request.style);
  result.edit_intent = normalized_model_edit_intent(QVariantMap{
      {"goalSummary", result.plan_summary}, {"globalChangePolicy", "preserve"}, {"colorMode", "preserve"},
      {"crop", "preserve"}, {"confidence", 1.0}, {"visualFindings", QVariantList{}},
      {"localizedActions", QVariantList{action}}});
  auto masks = localized_mask_stack_from_intent(result.edit_intent, request.style);
  if (masks.size() != 1) return {};
  auto mask = masks.first().toMap();
  mask.insert(QStringLiteral("agentRequireSemanticMask"), true);
  mask.insert(QStringLiteral("agentSemanticWholeRegion"), true);
  masks[0] = mask;
  result.recipe_patch = {{"masks", QVariantMap{{"appendMaskStack", masks}}}};
  result.prompt_audit = {{"backend", "local-command"}, {"plannerMode", "semantic-command-v1"},
      {"modelInvoked", false}, {"fallbackUsed", false}, {"globalChangePolicy", "preserve"},
      {"semanticGenerationRequired", true}, {"initialAdjustments", adjustments}};
  result.ok = true;
  return result;
}

AgentLocalModelPlanResult plan_agent_recipe_patch(
    const AgentLocalModelPlanRequest& request) {
  AgentLocalModelPlanResult result;
  result.model_id = request.model_id.trimmed().isEmpty() ? default_agent_local_model_id()
                                                         : request.model_id.trimmed();
  if (request.require_tool_call_plan &&
      (!request.allow_tool_call_plan ||
       !has_supported_tool_call_schema(request.available_tool_schemas))) {
    result.error_message = QStringLiteral(
        "A tool-call plan is required for unresolved approved composition work, but no supported document tools are available.");
    result.prompt_audit.insert(QStringLiteral("toolCallPlanRequired"), true);
    result.prompt_audit.insert(QStringLiteral("fallbackBlocked"), true);
    result.prompt_audit.insert(QStringLiteral("errorCode"),
                               QStringLiteral("required_tools_unavailable"));
    return result;
  }
  if (request.require_tool_call_plan &&
      result.model_id == QString::fromLatin1(kRulebookDeterministicModelId)) {
    result.error_message = QStringLiteral(
        "The selected deterministic planner cannot produce the required document-tool plan.");
    result.prompt_audit.insert(QStringLiteral("toolCallPlanRequired"), true);
    result.prompt_audit.insert(QStringLiteral("fallbackBlocked"), true);
    result.prompt_audit.insert(QStringLiteral("errorCode"),
                               QStringLiteral("required_tool_planner_unavailable"));
    return result;
  }
  if (request.asset.value(QStringLiteral("planningProofRequired")).toBool() &&
      image_payload_for_asset(request.asset, kDefaultOllamaPlanningImageEdge).base64.isEmpty()) {
    result.error_message = QStringLiteral("The completed planning proof is missing, changed or unreadable.");
    result.prompt_audit.insert(QStringLiteral("errorCode"), QStringLiteral("planning_proof_unavailable"));
    return result;
  }
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
      if (!request.require_tool_call_plan) {
        const auto semantic_plan = semantic_command_plan(request);
        if (semantic_plan.ok) return semantic_plan;
        const auto fast_visual_plan = fast_local_highlight_plan(request);
        if (fast_visual_plan.ok) {
          return fast_visual_plan;
        }
      }
      auto ollama_result = plan_ollama_recipe_patch(request, model);
      if (ollama_result.ok) {
        return ollama_result;
      }
      if (request.require_tool_call_plan) {
        ollama_result.prompt_audit.insert(QStringLiteral("toolCallPlanRequired"), true);
        ollama_result.prompt_audit.insert(QStringLiteral("fallbackBlocked"), true);
        ollama_result.prompt_audit.insert(QStringLiteral("fallbackBlockedReason"),
                                          QStringLiteral("required-tool-plan-not-produced"));
        return ollama_result;
      }
      const bool spatial_request = agent_instructions_require_grounded_edit(request.user_instructions);
      if (!request.allow_deterministic_fallback || spatial_request ||
          ollama_result.prompt_audit.value(QStringLiteral("errorCode")) == QStringLiteral("planning_proof_unavailable")) {
        ollama_result.error_message.replace(QStringLiteral("retry or use the fast fallback"),
            QStringLiteral("retry with another local vision model or a longer deadline"));
        ollama_result.prompt_audit.insert(QStringLiteral("fallbackBlocked"), true);
        ollama_result.prompt_audit.insert(QStringLiteral("fallbackBlockedReason"), QStringLiteral("confirmed-or-spatial-edit-requires-grounded-plan"));
        return ollama_result;
      }
      const auto transport = ollama_result.prompt_audit.value(QStringLiteral("transport")).toMap();
      const auto transport_error = transport.value(QStringLiteral("errorCode")).toString();
      const auto recoverable_transport_error =
          !transport_error.isEmpty() && transport_error != QStringLiteral("cancelled") &&
          transport_error != QStringLiteral("invalid_endpoint");
      const auto recoverable_model_output =
          ollama_result.error_message.contains(QStringLiteral("edit_intent response"), Qt::CaseInsensitive) ||
          ollama_result.error_message.contains(QStringLiteral("recipe_patch response"), Qt::CaseInsensitive) ||
          ollama_result.error_message.contains(QStringLiteral("valid JSON"), Qt::CaseInsensitive) ||
          recoverable_transport_error;
      if (!recoverable_model_output || transport_error == QStringLiteral("cancelled") ||
          (request.cancellation_requested && request.cancellation_requested())) {
        return ollama_result;
      }

      auto fallback_request = request;
      fallback_request.model_id = QString::fromLatin1(kRulebookDeterministicModelId);
      fallback_request.allow_tool_call_plan = false;
      fallback_request.available_tool_schemas.clear();
      auto fallback_result = plan_agent_recipe_patch(fallback_request);
      if (!fallback_result.ok) {
        return ollama_result;
      }
      auto fallback_audit = fallback_result.prompt_audit;
      fallback_audit.insert(QStringLiteral("fallbackUsed"), true);
      fallback_audit.insert(QStringLiteral("fallbackReason"), ollama_result.error_message);
      fallback_audit.insert(QStringLiteral("requestedModelId"), result.model_id);
      fallback_audit.insert(QStringLiteral("ollamaAttempt"), ollama_result.prompt_audit);
      fallback_result.prompt_audit = fallback_audit;
      fallback_result.warnings.prepend(
          QStringLiteral("The local LLM did not return a usable plan, so the app created a conservative validated draft with its built-in planner."));
      fallback_result.plan_summary = QStringLiteral("%1 Created with the safe local planner.")
                                         .arg(fallback_result.plan_summary);
      return fallback_result;
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
  const auto photographer_calibration =
      request.memory_packet
          .value(QStringLiteral("photographerCalibrationCard"))
          .toMap();
  const auto project_north_star =
      request.memory_packet.value(QStringLiteral("projectNorthStar")).toMap();
  const auto project_north_star_brief =
      project_north_star.value(QStringLiteral("brief")).toString();
  const auto combined_intent =
      QStringLiteral(
          "%1\n%2\n%3\nProject North Star: %4\nCurrent request: %5")
          .arg(style_id,
               goal,
               strategy_prompt,
               project_north_star_brief,
               request.user_instructions);
  result.edit_intent = deterministic_edit_intent(combined_intent, request.style);
  const auto explicit_user_intent =
      deterministic_edit_intent(request.user_instructions, {});
  const auto override_explicit_user_value = [
                                                &explicit_user_intent,
                                                &result](
                                                const QString& key,
                                                const QString& fallback) {
    const auto value = explicit_user_intent.value(key).toString();
    if (!value.isEmpty() && value != fallback) {
      result.edit_intent.insert(key, value);
    }
  };
  override_explicit_user_value(
      QStringLiteral("colorMode"), QStringLiteral("preserve"));
  override_explicit_user_value(
      QStringLiteral("contrast"), QStringLiteral("natural"));
  override_explicit_user_value(
      QStringLiteral("brightness"), QStringLiteral("preserve"));
  override_explicit_user_value(
      QStringLiteral("palette"), QStringLiteral("preserve"));
  override_explicit_user_value(
      QStringLiteral("saturation"), QStringLiteral("preserve"));
  override_explicit_user_value(
      QStringLiteral("crop"), QStringLiteral("preserve"));
  override_explicit_user_value(
      QStringLiteral("detail"), QStringLiteral("natural"));
  override_explicit_user_value(
      QStringLiteral("mood"), QStringLiteral("natural"));
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
      {QStringLiteral("photographerCalibrationAvailable"),
       photographer_calibration.value(QStringLiteral("available")).toBool()},
      {QStringLiteral("photographerCalibrationDigest"),
       photographer_calibration.value(QStringLiteral("profileDigestSha256"))},
      {QStringLiteral("projectNorthStarAvailable"),
       !project_north_star.isEmpty()},
      {QStringLiteral("projectNorthStarDigest"),
       project_north_star.value(QStringLiteral("digestSha256"))},
      {QStringLiteral("projectNorthStarCurrentRequestWins"), true},
      {QStringLiteral("imageIncluded"), false},
      {QStringLiteral("imageByteCount"), 0},
  };
  const auto max_exposure = number_constraint(
      request.style,
      QStringLiteral("max_exposure_shift"),
      1.0,
      0.05,
      2.0);
  auto max_saturation = number_constraint(
      request.style,
      QStringLiteral("max_saturation_shift"),
      16.0,
      1.0,
      40.0);
  auto max_temperature = number_constraint(
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

  const auto learned_profile = request.style.value(QStringLiteral("learnedProfile")).toMap().isEmpty()
                                   ? request.style.value(QStringLiteral("learned_profile")).toMap()
                                   : request.style.value(QStringLiteral("learnedProfile")).toMap();
  const auto learned_tendencies = learned_profile.value(QStringLiteral("editTendencies")).toMap().isEmpty()
                                      ? learned_profile.value(QStringLiteral("edit_tendencies")).toMap()
                                      : learned_profile.value(QStringLiteral("editTendencies")).toMap();
  const auto has_learned_tendencies = !learned_tendencies.isEmpty();
  if (has_learned_tendencies) {
    const auto apply_tendency = [&learned_tendencies](
                                    const QString& key,
                                    double* target) {
      auto ok = false;
      const auto value = learned_tendencies.value(key).toDouble(&ok);
      if (ok && std::isfinite(value)) {
        *target = value;
      }
    };
    apply_tendency(QStringLiteral("averageExposure"), &exposure);
    apply_tendency(QStringLiteral("averageContrast"), &contrast);
    apply_tendency(QStringLiteral("averageHighlights"), &highlights);
    apply_tendency(QStringLiteral("averageShadows"), &shadows);
    apply_tendency(QStringLiteral("averageTemperature"), &temperature);
    apply_tendency(QStringLiteral("averageTint"), &tint);
    apply_tendency(QStringLiteral("averageVibrance"), &vibrance);
    apply_tendency(QStringLiteral("averageSaturation"), &saturation);
    apply_tendency(QStringLiteral("averageClarity"), &clarity);
    max_saturation = std::max(
        max_saturation,
        std::min(100.0, std::max(std::abs(vibrance), std::abs(saturation))));
    max_temperature = std::max(max_temperature, std::min(100.0, std::abs(temperature)));
    result.plan_summary = QStringLiteral(
        "Reference-matched draft using the selected photos' learned tone and color adjustments.");
    result.prompt_audit.insert(QStringLiteral("learnedReferenceApplied"), true);
    result.prompt_audit.insert(
        QStringLiteral("learnedReferenceAssetCount"),
        learned_profile.value(QStringLiteral("source_asset_count")));
  }

  const auto monochrome_requested =
      result.edit_intent.value(QStringLiteral("colorMode")).toString() == QStringLiteral("monochrome");
  const auto stronger_contrast_requested =
      result.edit_intent.value(QStringLiteral("contrast")).toString() == QStringLiteral("strong");
  const auto softer_contrast_requested =
      result.edit_intent.value(QStringLiteral("contrast")).toString() == QStringLiteral("soft");
  const auto crop_requested =
      result.edit_intent.value(QStringLiteral("crop")).toString() != QStringLiteral("preserve");
  if (monochrome_requested) {
    saturation = -100.0;
    vibrance = 0.0;
    contrast = 16.0;
    result.plan_summary = QStringLiteral("Monochrome draft with stronger tonal separation.");
  }
  if (stronger_contrast_requested) {
    contrast = std::max(contrast, 42.0);
    highlights = std::min(highlights, -28.0);
    shadows = std::max(shadows, 18.0);
    whites = std::max(whites, 14.0);
    blacks = std::min(blacks, -28.0);
    curve_lights = std::max(curve_lights, 18.0);
    curve_highlights = std::min(curve_highlights, -14.0);
    result.plan_summary = monochrome_requested
                              ? QStringLiteral("High-contrast monochrome draft with strong tonal separation.")
                              : QStringLiteral("High-contrast draft with stronger whites, blacks, and tonal separation.");
  }
  if (softer_contrast_requested) {
    contrast = std::min(contrast, -24.0);
    highlights = std::max(highlights, -12.0);
    shadows = std::min(shadows, 8.0);
    whites = std::min(whites, 2.0);
    blacks = std::max(blacks, -4.0);
    curve_lights = std::min(curve_lights, 4.0);
    curve_highlights = std::max(curve_highlights, -4.0);
    saturation = -max_saturation;
    vibrance = -max_saturation;
    result.plan_summary = QStringLiteral(
        "Quiet low-contrast draft with restrained color and gentle tonal separation.");
  }
  if (result.edit_intent.value(QStringLiteral("brightness")).toString() == QStringLiteral("brighter")) {
    exposure = std::max(exposure, 0.18);
    shadows += 8.0;
  }
  if (result.edit_intent.value(QStringLiteral("mood")).toString() == QStringLiteral("moody") ||
      result.edit_intent.value(QStringLiteral("brightness")).toString() == QStringLiteral("darker")) {
    exposure = std::min(exposure, -0.18);
    blacks -= 10.0;
    contrast += 8.0;
  }
  if (result.edit_intent.value(QStringLiteral("detail")).toString() == QStringLiteral("soft")) {
    clarity -= 4.0;
    texture -= 2.0;
    sharpening = 12.0;
  }

  exposure = bounded(exposure, max_exposure);
  const auto palette_intent =
      result.edit_intent.value(QStringLiteral("palette")).toString();
  const auto explicit_palette_intent =
      explicit_user_intent.value(QStringLiteral("palette")).toString();
  const auto apply_semantic_palette =
      !has_learned_tendencies ||
      (explicit_palette_intent != QStringLiteral("preserve") &&
       !explicit_palette_intent.isEmpty());
  if (apply_semantic_palette && palette_intent == QStringLiteral("warm")) {
    temperature = std::max(temperature, std::min(8.0, max_temperature));
  } else if (apply_semantic_palette && palette_intent == QStringLiteral("cool")) {
    temperature = std::min(temperature, -std::min(8.0, max_temperature));
  } else if (apply_semantic_palette &&
             palette_intent == QStringLiteral("neutral") &&
             !has_learned_tendencies) {
    temperature = 0.0;
  } else if (temperature_bias == QStringLiteral("warm")) {
    temperature = std::max(temperature, std::min(8.0, max_temperature));
  } else if (temperature_bias == QStringLiteral("cool")) {
    temperature = std::min(temperature, -std::min(8.0, max_temperature));
  } else if (temperature_bias == QStringLiteral("neutral") && !has_learned_tendencies) {
    temperature = 0.0;
  }
  temperature = bounded(temperature, max_temperature);
  contrast = std::clamp(contrast, -100.0, 100.0);
  highlights = std::clamp(highlights, -100.0, 100.0);
  shadows = std::clamp(shadows, -100.0, 100.0);
  saturation = monochrome_requested ? -100.0 : bounded(saturation, max_saturation);
  vibrance = std::clamp(vibrance, -max_saturation, max_saturation);

  const auto current_patch_document =
      QJsonDocument::fromJson(request.current_adjustment_patch_json.toUtf8());
  const auto current_adjustments =
      current_patch_document.object()
          .value(QStringLiteral("adjustmentState"))
          .toObject()
          .value(QStringLiteral("globalAdjustments"))
          .toObject();
  const auto has_current_adjustments = !current_adjustments.isEmpty();
  const auto current_number = [&current_adjustments](
                                  const QString& key,
                                  double fallback) {
    return current_adjustments.contains(key)
               ? current_adjustments.value(key).toDouble(fallback)
               : fallback;
  };
  const auto explicit_brightness =
      explicit_user_intent.value(QStringLiteral("brightness")).toString();
  const auto explicit_contrast =
      explicit_user_intent.value(QStringLiteral("contrast")).toString();
  const auto explicit_palette =
      explicit_user_intent.value(QStringLiteral("palette")).toString();
  const auto explicit_saturation =
      explicit_user_intent.value(QStringLiteral("saturation")).toString();
  const auto normalized_user_instructions =
      request.user_instructions.trimmed().toLower();
  const auto preserve_tone_requested =
      normalized_user_instructions.contains(QStringLiteral("tone unchanged")) ||
      normalized_user_instructions.contains(QStringLiteral("preserve tone")) ||
      normalized_user_instructions.contains(QStringLiteral("preserve the tone")) ||
      normalized_user_instructions.contains(QStringLiteral("keep the tone"));
  const auto direct_tone_request =
      explicit_brightness == QStringLiteral("brighter") ||
      explicit_brightness == QStringLiteral("darker") ||
      explicit_contrast == QStringLiteral("strong") ||
      explicit_contrast == QStringLiteral("soft") ||
      preserve_tone_requested;
  const auto direct_color_request =
      explicit_palette == QStringLiteral("warm") ||
      explicit_palette == QStringLiteral("cool") ||
      explicit_palette == QStringLiteral("neutral") ||
      explicit_saturation == QStringLiteral("more") ||
      explicit_saturation == QStringLiteral("less");

  if (has_current_adjustments && direct_tone_request) {
    exposure = current_number(QStringLiteral("exposure"), exposure);
    contrast = current_number(QStringLiteral("contrast"), contrast);
    highlights = current_number(QStringLiteral("highlights"), highlights);
    shadows = current_number(QStringLiteral("shadows"), shadows);
    whites = current_number(QStringLiteral("whites"), whites);
    blacks = current_number(QStringLiteral("blacks"), blacks);
    if (!preserve_tone_requested) {
      if (explicit_brightness == QStringLiteral("brighter")) {
        exposure = std::clamp(
            exposure + std::min(0.35, max_exposure), -5.0, 5.0);
        shadows = std::clamp(shadows + 6.0, -100.0, 100.0);
      } else if (explicit_brightness == QStringLiteral("darker")) {
        exposure = std::clamp(
            exposure - std::min(0.35, max_exposure), -5.0, 5.0);
        blacks = std::clamp(blacks - 6.0, -100.0, 100.0);
      }
      if (explicit_contrast == QStringLiteral("strong")) {
        contrast = std::clamp(
            std::max(contrast + 24.0, 42.0), -100.0, 100.0);
        whites = std::clamp(whites + 8.0, -100.0, 100.0);
        blacks = std::clamp(blacks - 8.0, -100.0, 100.0);
      } else if (explicit_contrast == QStringLiteral("soft")) {
        contrast = std::clamp(contrast - 24.0, -100.0, 100.0);
        whites = std::clamp(whites - 5.0, -100.0, 100.0);
        blacks = std::clamp(blacks + 5.0, -100.0, 100.0);
      }
    }
  }
  if (has_current_adjustments && direct_color_request) {
    temperature = current_number(QStringLiteral("temperature"), temperature);
    tint = current_number(QStringLiteral("tint"), tint);
    vibrance = current_number(QStringLiteral("vibrance"), vibrance);
    saturation = current_number(QStringLiteral("saturation"), saturation);
    if (explicit_palette == QStringLiteral("warm")) {
      temperature = std::clamp(
          temperature + std::min(8.0, max_temperature), -100.0, 100.0);
    } else if (explicit_palette == QStringLiteral("cool")) {
      temperature = std::clamp(
          temperature - std::min(8.0, max_temperature), -100.0, 100.0);
    } else if (explicit_palette == QStringLiteral("neutral")) {
      temperature = 0.0;
      tint = 0.0;
    }
    if (explicit_saturation == QStringLiteral("more")) {
      vibrance = std::clamp(
          vibrance + std::min(12.0, max_saturation), -100.0, 100.0);
      saturation = std::clamp(
          saturation + std::min(6.0, max_saturation), -100.0, 100.0);
    } else if (explicit_saturation == QStringLiteral("less")) {
      vibrance = std::clamp(
          vibrance - std::min(12.0, max_saturation), -100.0, 100.0);
      saturation = std::clamp(
          saturation - std::min(6.0, max_saturation), -100.0, 100.0);
    }
  }

  if (photographer_calibration.value(QStringLiteral("available")).toBool()) {
    const auto issues =
        photographer_calibration.value(QStringLiteral("issueTagCounts")).toMap();
    const auto concern_keys = photographer_calibration
                                  .value(QStringLiteral("validationConcernKeys"))
                                  .toStringList();
    auto brightness_factor = 1.0;
    auto contrast_factor = 1.0;
    auto warmth_factor = 1.0;
    auto saturation_factor = 1.0;
    if (concern_keys.contains(QStringLiteral("brightness"))) {
      brightness_factor = 0.84;
    }
    if (concern_keys.contains(QStringLiteral("contrast")) ||
        concern_keys.contains(QStringLiteral("preserveContrast"))) {
      contrast_factor = 0.84;
    }
    if (concern_keys.contains(QStringLiteral("warmth"))) {
      warmth_factor = 0.84;
    }
    if (concern_keys.contains(QStringLiteral("saturation")) ||
        concern_keys.contains(QStringLiteral("preserveSaturation"))) {
      saturation_factor = 0.84;
    }
    if (issues.contains(QStringLiteral("too-bright")) &&
        exposure > current_number(QStringLiteral("exposure"), 0.0)) {
      brightness_factor = std::min(brightness_factor, 0.7);
    }
    if (issues.contains(QStringLiteral("too-dark")) &&
        exposure < current_number(QStringLiteral("exposure"), 0.0)) {
      brightness_factor = std::min(brightness_factor, 0.7);
    }
    if (issues.contains(QStringLiteral("too-contrasty")) &&
        contrast > current_number(QStringLiteral("contrast"), 0.0)) {
      contrast_factor = std::min(contrast_factor, 0.7);
    }
    if (issues.contains(QStringLiteral("too-flat")) &&
        contrast < current_number(QStringLiteral("contrast"), 0.0)) {
      contrast_factor = std::min(contrast_factor, 0.7);
    }
    if (issues.contains(QStringLiteral("too-warm")) &&
        temperature > current_number(QStringLiteral("temperature"), 0.0)) {
      warmth_factor = std::min(warmth_factor, 0.7);
    }
    if (issues.contains(QStringLiteral("too-cool")) &&
        temperature < current_number(QStringLiteral("temperature"), 0.0)) {
      warmth_factor = std::min(warmth_factor, 0.7);
    }
    if (issues.contains(QStringLiteral("too-saturated")) &&
        (vibrance > current_number(QStringLiteral("vibrance"), 0.0) ||
         saturation > current_number(QStringLiteral("saturation"), 0.0))) {
      saturation_factor = std::min(saturation_factor, 0.7);
    }
    const auto restrain = [&current_number](
                              double value,
                              const QString& key,
                              double factor) {
      const auto baseline = current_number(key, 0.0);
      return baseline + ((value - baseline) * factor);
    };
    QStringList adjusted_groups;
    if (brightness_factor < 1.0) {
      exposure = restrain(
          exposure, QStringLiteral("exposure"), brightness_factor);
      shadows = restrain(
          shadows, QStringLiteral("shadows"), brightness_factor);
      adjusted_groups.append(QStringLiteral("brightness"));
    }
    if (contrast_factor < 1.0) {
      contrast = restrain(
          contrast, QStringLiteral("contrast"), contrast_factor);
      whites = restrain(
          whites, QStringLiteral("whites"), contrast_factor);
      blacks = restrain(
          blacks, QStringLiteral("blacks"), contrast_factor);
      adjusted_groups.append(QStringLiteral("contrast"));
    }
    if (warmth_factor < 1.0) {
      temperature = restrain(
          temperature, QStringLiteral("temperature"), warmth_factor);
      tint = restrain(tint, QStringLiteral("tint"), warmth_factor);
      adjusted_groups.append(QStringLiteral("warmth"));
    }
    if (saturation_factor < 1.0 && !monochrome_requested) {
      vibrance = restrain(
          vibrance, QStringLiteral("vibrance"), saturation_factor);
      saturation = restrain(
          saturation, QStringLiteral("saturation"), saturation_factor);
      adjusted_groups.append(QStringLiteral("saturation"));
    }
    result.prompt_audit.insert(
        QStringLiteral("photographerCalibrationApplied"), true);
    result.prompt_audit.insert(
        QStringLiteral("photographerCalibrationAdjustedGroups"),
        adjusted_groups);
    result.prompt_audit.insert(
        QStringLiteral("photographerCalibrationRequiresReview"),
        !concern_keys.isEmpty());
    result.prompt_audit.insert(
        QStringLiteral("photographerCalibrationNumericThresholdOverrides"),
        false);
    if (!adjusted_groups.isEmpty()) {
      result.plan_summary += QStringLiteral(
          " Response strength was restrained by this project's blind photographer calibration.");
      result.warnings.append(QStringLiteral(
          "Photographer calibration influenced response strength; the rendered proof still requires your review."));
    }
  }

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
  if (crop_requested && style_tool_enabled(request.style, QStringLiteral("crop"))) {
    const auto tighter_crop =
        result.edit_intent.value(QStringLiteral("crop")).toString() == QStringLiteral("tighter");
    const auto crop_inset = tighter_crop ? 0.09 : 0.05;
    const auto crop_extent = tighter_crop ? 0.82 : 0.92;
    recipe_patch.insert(
        QStringLiteral("crop"),
        QVariantMap{
            {QStringLiteral("cropPreset"), QStringLiteral("Custom")},
            {QStringLiteral("cropX"), crop_inset},
            {QStringLiteral("cropY"), crop_inset},
            {QStringLiteral("cropWidth"), crop_extent},
            {QStringLiteral("cropHeight"), crop_extent},
            {QStringLiteral("cropZoom"), 1.0},
        });
    result.plan_summary += QStringLiteral(" Added a centered tighter crop to strengthen the composition.");
  }
  if (!monochrome_requested && !direct_color_request &&
      style_tool_enabled(request.style, QStringLiteral("hsl")) &&
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
  if (!monochrome_requested && !direct_color_request &&
      style_tool_enabled(request.style, QStringLiteral("color_grading")) &&
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
