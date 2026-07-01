#include "agent_local_model.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

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

AgentLocalModelPlanResult plan_agent_recipe_patch(
    const AgentLocalModelPlanRequest& request) {
  AgentLocalModelPlanResult result;
  result.model_id = request.model_id.trimmed().isEmpty() ? default_agent_local_model_id()
                                                         : request.model_id.trimmed();
  if (result.model_id != QString::fromLatin1(kRulebookDeterministicModelId)) {
    result.error_message = QStringLiteral("Unsupported local model: %1").arg(result.model_id);
    return result;
  }
  if (request.style.isEmpty()) {
    result.error_message = QStringLiteral("A valid style is required before planning an agent edit.");
    return result;
  }

  const auto style_id = request.style.value(QStringLiteral("id")).toString();
  const auto goal = request.style.value(QStringLiteral("goal")).toString();
  const auto combined_intent =
      QStringLiteral("%1\n%2\n%3").arg(style_id, goal, request.user_instructions);
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
  result.ok = true;
  return result;
}

}  // namespace photo_editor::desktop
