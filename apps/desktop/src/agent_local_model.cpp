#include "agent_local_model.h"
#include "agent_runtime_paths.h"
#include "agent_model_artifact.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QMetaType>
#include <QRegularExpression>
#include <QSet>
#include <QTcpSocket>
#include <QThread>
#include <QUrl>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>

#include "agent_edit_recipe_patch.h"

namespace photo_editor::desktop {
namespace {

constexpr auto kRulebookDeterministicModelId = "rulebook-deterministic";
constexpr auto kFastLocalVisionPlannerId = "fast-local-vision";
constexpr auto kCloudModelOptInEnv = "PHOTO_EDITOR_ENABLE_CLOUD_MODELS";
constexpr int kDefaultOllamaRequestTimeoutMs = 30000;
constexpr int kMinimumOllamaRequestTimeoutMs = 30000;
constexpr int kMaximumOllamaRequestTimeoutMs = 600000;
constexpr int kDefaultOllamaContextTokens = 12288;
constexpr int kDefaultOllamaMaxImageEdge = 768;
constexpr int kDefaultOllamaPlanningImageEdge = 512;
constexpr int kDefaultOllamaPlanningMaxOutputTokens = 640;

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
    bool image_included) {
  const auto configured = ollama_context_tokens(model);
  const auto estimated_text_tokens = static_cast<int>(prompt_char_count / 3);
  const auto visual_and_output_reserve = image_included ? 5120 : 2048;
  const auto estimated_required = estimated_text_tokens + visual_and_output_reserve;
  const auto rounded_required =
      ((std::max(estimated_required, 4096) + 4095) / 4096) * 4096;
  return std::clamp(std::max(configured, rounded_required), 4096, 24576);
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
    if (backend == QStringLiteral("ollama")) {
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
  if (preserves_color && !requests_monochrome) {
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
    const std::function<bool()>& cancellation_requested = {}) {
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
    const auto index = std::min(fake_sequence_index, static_cast<int>(sequence.size()) - 1);
    ++fake_sequence_index;
    const auto response = sequence.at(index).toObject();
    if (response.isEmpty()) {
      return fail(
          QStringLiteral("invalid_fake_response"),
          QStringLiteral("Fake Ollama sequence response is not an object."));
    }
    finish_transport(true, {}, {}, 200, 0, false);
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
  if (!socket.waitForConnected(timeout_ms)) {
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
  while (socket.bytesToWrite() > 0 && write_timer.elapsed() < timeout_ms) {
    if (cancellation_requested && cancellation_requested()) {
      socket.abort();
      return fail(
          QStringLiteral("cancelled"),
          QStringLiteral("Local model request canceled."),
          0,
          0,
          true);
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
  QElapsedTimer timer;
  timer.start();
  auto response_complete = false;
  while (timer.elapsed() < timeout_ms) {
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
    const auto original_bytes = file.readAll();
    QImage image;
    image.loadFromData(original_bytes);
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

QJsonObject ollama_response_payload_schema(bool allow_tool_call_plan) {
  QJsonObject properties{
      {QStringLiteral("decision"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
      {QStringLiteral("summary"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
      {QStringLiteral("styleConstraintCitations"),
       QJsonObject{
           {QStringLiteral("type"), QStringLiteral("array")},
       }},
      {QStringLiteral("editIntent"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
      {QStringLiteral("toolCalls"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
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
  return QJsonObject{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("properties"), properties},
      {QStringLiteral("required"), required},
  };
}

QJsonObject ollama_generation_payload_schema(bool allow_tool_call_plan) {
  if (allow_tool_call_plan) {
    return ollama_response_payload_schema(allow_tool_call_plan);
  }
  return QJsonObject{
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
  if (value.typeId() == QMetaType::QByteArray || key.endsWith(QStringLiteral("Data"))) {
    return QVariantMap{{"storedLocally", true}, {"payloadOmitted", true}};
  }
  if (key == QStringLiteral("promptAudit") || key == QStringLiteral("modelPromptAudit"))
    return QStringLiteral("[prior prompt audit omitted]");
  if (value.typeId() == QMetaType::QVariantMap) {
    QVariantMap result;
    const auto map = value.toMap();
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

QString ollama_agent_prompt(const AgentLocalModelPlanRequest& request) {
  const auto style_json =
      prompt_json(request.style);
  const auto available_tools_json =
      QString::fromUtf8(QJsonDocument::fromVariant(request.available_tool_schemas).toJson(QJsonDocument::Compact));
  const auto prior_tool_results_json =
      prompt_json(request.prior_tool_results);
  const auto memory_packet_json =
      prompt_json(request.memory_packet);
  const auto examples = request.few_shot_examples.isEmpty()
                            ? compact_few_shot_examples_from_memory(request.memory_packet)
                            : request.few_shot_examples;
  const auto few_shot_examples_json =
      prompt_json(examples);
  const auto strategy_phase = normalized_strategy_phase(request.strategy_phase);
  const auto strategy_prompt = strategy_prompt_for_phase(strategy_phase);
  return QStringLiteral(
      "You are a local photo-editing planning agent. Return JSON only. "
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
      "When current-proof validation evidence is available in prior tool results, use imageStats.histogram and imageStats.vectorscope to decide tone, white balance, saturation, skin, mask, and layer strategy. "
      "If you already have enough context, return decision=edit_intent with a semantic editIntent. "
      "Ground visualFindings in visible evidence from the supplied image, name the affected target, and use normalized center/radius coordinates. For an arbitrary visible item such as a jacket, sign, window, flower, or specific person, set target=object and copy the concise visual noun phrase into targetLabel. "
      "Use localizedActions only when a bounded local change can improve one target without harming another; include at most two, require confidence of at least 0.6, and leave the array empty when localization is uncertain. "
      "Set selectionMode=semantic only when the intended selection is the complete visible subject, sky, or background. Set selectionMode=guided for a clearly visible arbitrary object with target=object and a specific targetLabel. If the user's phrase plausibly matches two or three visible instances and you cannot identify the exact instance with high confidence, include selectionCandidates for each plausible instance with a concise distinct label, confidence, and evidence region; do not guess silently. Omit selectionCandidates when one instance is clearly intended. Use region for a soft spatial area rather than an object. Always provide the evidence region because the app uses it for review and as a fail-closed radial fallback. "
      "Set globalChangePolicy=preserve when the user asks to change only one region or keep the rest unchanged; otherwise use allow. Preserving framing or protecting highlights does not forbid an explicitly requested overall look, so use allow for that combination. "
      "For an explicit poster, flyer, or social-graphic request, preserve supplied copy exactly and use the available recipe-patch schema to create editable graphics.textLayers; keep the photo and user layers intact, and do not simulate typography with image-tone adjustments. If the user has not supplied essential copy, ask for it rather than inventing names, dates, prices, or claims. If graphic patch tools are unavailable, state that limitation and do not claim to have created a graphic. "
      "For any requested layer, text, or photo-effect operation, inspect the current recipe and call photo.get_recipe_patch_schema before planning; target existing layers by their exact ids, use only supported effect types and typed fields, preserve unrelated layer properties, validate the patch, and verify the rendered draft. Do not invent a successful operation when the selected layer is unavailable. "
      "localizedActions.adjustments is the only place you may provide bounded numeric adjustment deltas; the app clamps them and compiles non-destructive masks. Treat semantic and guided selection as requests until the app reports that its local selection runtime generated and validated the mask. "
      "Use retouchActions only for an explicit user request to heal a small blemish or remove a small, clearly identified visible distraction. Return an empty array for general enhancement, inferred cleanup, ambiguous targets, large regions, or confidence below 0.85. Use kind=blemish with operation=heal, and kind=distraction or object with operation=remove. Never infer face or body retouching. Set globalChangePolicy=preserve for a retouch-only request. The app compiles retouchActions into append-only cleanup operations and always requires user approval before creating the draft. If the style disallows retouch, return cannot_complete_safely. "
      "For the global edit intent describe the photographic outcome only; never author recipe fields, recipePatch, or file mutations. "
      "Keep the response compact and omit optional editIntent fields that do not change the requested result. "
      "If you need more context and tool calls are allowed, return decision=tool_call_plan with toolCalls. "
      "If user intent is ambiguous, return decision=needs_user_input with userQuestions. "
      "If the edit cannot be completed safely under the rulebook, return decision=cannot_complete_safely with reason. "
      "After tool results are provided, return decision=edit_intent. "
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
      "Prior MCP tool results JSON:\n%10")
      .arg(
          strategy_phase,
          strategy_prompt,
          request.user_instructions,
          prompt_json(QJsonDocument::fromJson(request.current_adjustment_patch_json.toUtf8()).toVariant()),
          style_json,
          memory_packet_json,
          few_shot_examples_json,
          request.allow_tool_call_plan ? QStringLiteral("true") : QStringLiteral("false"),
          available_tools_json,
          prior_tool_results_json);
}

QString ollama_proof_critique_prompt(const AgentLocalModelProofCritiqueRequest& request) {
  const auto style_json =
      prompt_json(request.style);
  const auto graphic_layers_json =
      prompt_json(request.graphic_layers);
  const auto validation_json =
      prompt_json(request.validation_report);
  const auto attempts_json =
      prompt_json(request.previous_attempts);
  const auto calibration_json = prompt_json(request.photographer_calibration);
  const auto project_north_star_json = prompt_json(request.project_north_star);
  return QStringLiteral(
      "You are a local photo-editing proof evaluator. Return JSON only. "
      "Do not propose a recipePatch and do not mutate files. Binary mask/image payloads remain local and are intentionally omitted from metadata. "
      "If relevant evidence was omitted for size or is unavailable, request human review rather than claiming verification. "
      "Evaluate whether the rendered after proof satisfies the user instructions, active style rulebook, and deterministic validation report. "
      "Graphic proof gate: when expectedGraphicTextLayers is non-empty, compare every expected text string with the rendered after image. Check that copy is complete and unclipped, readable at intended output size, has adequate contrast and hierarchy, respects safe margins, avoids collisions, and does not obscure the subject or focal area. If a string is missing, altered, clipped, colliding, or illegible, fail with a specific issue and repair hint. If proof resolution is insufficient to verify exact copy or layout, set needsUserReview=true and do not claim a pass. Correct metadata alone is not proof that rendered typography is correct. "
      "Use validationReport.imageStats.histogram like a histogram and validationReport.imageStats.vectorscope like a vectorscope; treat clipping, compressed tonal range, channel cast, hue imbalance, and skin drift as reasons to fail or ask for repair. "
      "Use pass=false for unresolved clipping, severe color cast, implausible skin, unsafe retouch, broken masks, or obvious style mismatch. "
      "Use repairHints as concise guidance for a later semantic-intent repair turn; never include raw image data or direct recipe JSON.\n"
      "Use photographer calibration only as project-local qualitative evidence. If the current proof touches one of its validationConcernKeys, be conservative and set needsUserReview=true when taste remains uncertain. Never change numeric thresholds or claim that calibration proves professional readiness.\n"
      "Use the Project North Star only as an explicitly authored qualitative review goal. The current request, active style constraints, deterministic validation, and safety policy win every conflict. Never fail a proof solely because it does not invent a numeric or destructive change from the North Star.\n"
      "User instructions:\n%1\n"
      "Style rulebook JSON:\n%2\n"
      "Validation report JSON:\n%3\n"
      "Previous attempts JSON:\n%4\n"
      "Photographer calibration JSON:\n%5\n"
      "Project North Star JSON:\n%6\n"
      "Expected graphic text layers JSON:\n%7")
      .arg(
          request.user_instructions,
          style_json,
          validation_json,
          attempts_json,
          calibration_json,
          project_north_star_json,
          graphic_layers_json);
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
  const auto photographer_calibration =
      request.memory_packet
          .value(QStringLiteral("photographerCalibrationCard"))
          .toMap();
  const auto project_north_star =
      request.memory_packet.value(QStringLiteral("projectNorthStar")).toMap();
  const auto system_prompt =
      QStringLiteral("Return one compact JSON object matching the supplied schema. ") +
      (request.allow_tool_call_plan
           ? QStringLiteral("You may return decision=tool_call_plan when one of the supplied tools is genuinely required. ")
           : QStringLiteral("Tool calls are disabled for this turn; you must not return decision=tool_call_plan. ")) +
      QStringLiteral("For an edit_intent response include summary, styleConstraintCitations, editIntent, and optional warnings. ") +
      QStringLiteral("The app owns recipe compilation and application.");
  QJsonObject user_message{
      {QStringLiteral("role"), QStringLiteral("user")},
      {QStringLiteral("content"), prompt},
  };
  const auto image_payload = image_payload_for_asset(
      request.asset,
      ollama_planning_image_edge(model));
  const auto image_included = !image_payload.base64.isEmpty() && model.value(QStringLiteral("vision")).toBool();
  const auto context_tokens =
      ollama_effective_context_tokens(model, prompt.size(), image_included);
  const auto generation_schema =
      ollama_generation_payload_schema(request.allow_tool_call_plan);
  const auto planning_max_output_tokens =
      ollama_planning_max_output_tokens(model);
  if (image_included) {
    user_message.insert(QStringLiteral("images"), QJsonArray{image_payload.base64});
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
       QStringLiteral("agent-compact-semantic-intent-generation-v1")},
      {QStringLiteral("generationSchemaByteCount"),
       QJsonDocument(generation_schema).toJson(QJsonDocument::Compact).size()},
      {QStringLiteral("generationSchema"), generation_schema.toVariantMap()},
      {QStringLiteral("imageSourceKind"), image_payload.source_kind},
      {QStringLiteral("imageSourcePath"), image_payload.source_path},
      {QStringLiteral("responseSchema"),
       ollama_response_payload_schema(request.allow_tool_call_plan).toVariantMap()},
  };

  QString error_message;
  QVariantMap transport_report;
  const auto response = http_post_json(
      endpoint,
      QStringLiteral("/api/chat"),
      payload,
      ollama_request_timeout_ms(model),
      &error_message,
      &transport_report,
      request.cancellation_requested);
  result.prompt_audit.insert(QStringLiteral("transport"), transport_report);
  if (response.isEmpty()) {
    result.error_message = error_message.isEmpty()
                               ? QStringLiteral("Ollama did not return a response.")
                               : error_message;
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
  QString response_source;
  const auto content = ollama_structured_response_text(response, &response_source);
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
  result.prompt_audit.insert(QStringLiteral("responseDecision"),
                             content_object.value(QStringLiteral("decision")).toString());
  result.prompt_audit.insert(QStringLiteral("responseHasEditIntent"),
                             content_object.value(QStringLiteral("editIntent")).isObject() &&
                                 !content_object.value(QStringLiteral("editIntent")).toObject().isEmpty());
  result.prompt_audit.insert(QStringLiteral("responseCitationCount"),
                             content_object.value(QStringLiteral("styleConstraintCitations")).toArray().size());
  result.decision = content_object.value(QStringLiteral("decision")).toString(QStringLiteral("edit_intent")).trimmed();
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
  if (result.decision.isEmpty() || result.decision == QStringLiteral("edit_intent")) {
    result.decision = QStringLiteral("edit_intent");
  }
  if (result.decision != QStringLiteral("edit_intent")) {
    result.error_message = QStringLiteral("Unsupported Ollama planning decision: %1").arg(result.decision);
    return result;
  }
  if (result.style_constraint_citations.isEmpty() || result.edit_intent.isEmpty()) {
    result.error_message = QStringLiteral("Ollama edit_intent response must include style citations and an editIntent.");
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
  const auto models = available_agent_local_models();
  for (const auto& model_value : models) {
    const auto model = model_value.toMap();
    if (model.value(QStringLiteral("backend")).toString() == QStringLiteral("ollama") &&
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
  // Expected binary-mask omission is not missing visual evidence: the rendered
  // after image is required separately. Truncated qualitative/validation
  // metadata, however, must not be waived by an optimistic model response.
  const auto metadata_evidence_complete =
      !prompt.contains(QStringLiteral("\"evidenceOmittedForSize\":true")) &&
      !prompt.contains(QStringLiteral("[overlong text omitted:")) &&
      !prompt.contains(QStringLiteral("[nested evidence omitted]")) &&
      !prompt.contains(QStringLiteral("[additional entries omitted]"));
  const auto system_prompt =
      QStringLiteral("Return one compact JSON object with pass, summary, issues, repairHints, needsUserReview, tasteConfidence, and optional warnings.");
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
  const auto image_included = !image_payload.base64.isEmpty();
  const auto context_tokens =
      ollama_effective_context_tokens(model, prompt.size(), image_included);
  if (image_included) {
    user_message.insert(QStringLiteral("images"), QJsonArray{image_payload.base64});
  }
  const QJsonObject payload{
      {QStringLiteral("model"), model_name},
      {QStringLiteral("stream"), false},
      {QStringLiteral("think"), false},
      {QStringLiteral("keep_alive"), QStringLiteral("10m")},
      {QStringLiteral("format"), ollama_proof_critique_payload_schema()},
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
      {QStringLiteral("responseSchema"), ollama_proof_critique_payload_schema().toVariantMap()},
  };
  if (!image_included) {
    result.error_message = QStringLiteral("No rendered proof image was available for model critique.");
    return result;
  }

  QString error_message;
  QVariantMap transport_report;
  const auto response = http_post_json(
      endpoint,
      QStringLiteral("/api/chat"),
      payload,
      ollama_request_timeout_ms(model),
      &error_message,
      &transport_report,
      request.cancellation_requested);
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
      !metadata_evidence_complete) result.pass = false;
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
      const auto semantic_plan = semantic_command_plan(request);
      if (semantic_plan.ok) return semantic_plan;
      const auto fast_visual_plan = fast_local_highlight_plan(request);
      if (fast_visual_plan.ok) {
        return fast_visual_plan;
      }
      auto ollama_result = plan_ollama_recipe_patch(request, model);
      if (ollama_result.ok) {
        return ollama_result;
      }
      const bool spatial_request = agent_instructions_require_grounded_edit(request.user_instructions);
      if (!request.allow_deterministic_fallback || spatial_request) {
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
