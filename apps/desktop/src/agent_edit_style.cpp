#include "agent_edit_style.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSaveFile>

namespace photo_editor::desktop {
namespace {

QString trimmed_string(const QJsonObject& object, const QString& key) {
  return object.value(key).toString().trimmed();
}

bool is_non_empty_string(const QJsonObject& object, const QString& key) {
  return object.value(key).isString() && !trimmed_string(object, key).isEmpty();
}

void require_object(
    const QJsonObject& object,
    const QString& key,
    QStringList* errors) {
  if (!object.value(key).isObject()) {
    errors->append(QStringLiteral("%1 must be an object").arg(key));
  }
}

QVariantMap object_map(const QJsonObject& object, const QString& key) {
  return object.value(key).toObject().toVariantMap();
}

bool tool_enabled(const QJsonObject& allowed_tools, const QString& key) {
  const auto value = allowed_tools.value(key);
  if (value.isBool()) {
    return value.toBool();
  }
  if (value.isString()) {
    return !value.toString().trimmed().isEmpty() &&
           value.toString().trimmed() != QStringLiteral("false") &&
           value.toString().trimmed() != QStringLiteral("off") &&
           value.toString().trimmed() != QStringLiteral("none");
  }
  return false;
}

bool executable_style_key(const QString& key) {
  auto normalized = key.trimmed().toLower();
  normalized.remove(QLatin1Char('_'));
  normalized.remove(QLatin1Char('-'));
  normalized.remove(QLatin1Char(' '));
  static const QStringList forbidden = {
      QStringLiteral("code"),
      QStringLiteral("command"),
      QStringLiteral("commands"),
      QStringLiteral("exec"),
      QStringLiteral("execute"),
      QStringLiteral("executable"),
      QStringLiteral("hook"),
      QStringLiteral("hooks"),
      QStringLiteral("onapply"),
      QStringLiteral("ondraft"),
      QStringLiteral("oncommit"),
      QStringLiteral("postprocess"),
      QStringLiteral("preprocess"),
      QStringLiteral("script"),
      QStringLiteral("scripts"),
      QStringLiteral("shell"),
      QStringLiteral("subprocess"),
      QStringLiteral("system"),
      QStringLiteral("webhook"),
      QStringLiteral("webhooks"),
  };
  return forbidden.contains(normalized);
}

bool unsafe_style_reference_payload_key(const QString& key) {
  auto normalized = key.trimmed().toLower();
  normalized.remove(QLatin1Char('_'));
  normalized.remove(QLatin1Char('-'));
  normalized.remove(QLatin1Char(' '));
  static const QStringList unsafe = {
      QStringLiteral("base64"),
      QStringLiteral("binary"),
      QStringLiteral("bytes"),
      QStringLiteral("dataurl"),
      QStringLiteral("embeddedimage"),
      QStringLiteral("imagebytes"),
      QStringLiteral("imagepayload"),
      QStringLiteral("rawbytes"),
      QStringLiteral("thumbnaildata"),
      QStringLiteral("thumbnaildataurl"),
  };
  return unsafe.contains(normalized);
}

void append_executable_style_payload_errors(
    const QJsonValue& value,
    const QString& path,
    QStringList* errors) {
  if (errors == nullptr) {
    return;
  }
  if (value.isObject()) {
    const auto object = value.toObject();
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
      const auto child_path = path.isEmpty() ? iterator.key()
                                             : QStringLiteral("%1.%2").arg(path, iterator.key());
      if (executable_style_key(iterator.key())) {
        errors->append(
            QStringLiteral("Style must not include executable or code-like payload field: %1")
                .arg(child_path));
      }
      append_executable_style_payload_errors(iterator.value(), child_path, errors);
    }
    return;
  }
  if (value.isArray()) {
    const auto array = value.toArray();
    for (int index = 0; index < array.size(); ++index) {
      append_executable_style_payload_errors(
          array.at(index),
          QStringLiteral("%1[%2]").arg(path).arg(index),
          errors);
    }
  }
}

void append_unsafe_reference_payload_errors(
    const QJsonValue& value,
    const QString& path,
    QStringList* errors) {
  if (errors == nullptr) {
    return;
  }
  if (value.isObject()) {
    const auto object = value.toObject();
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
      const auto child_path = path.isEmpty() ? iterator.key()
                                             : QStringLiteral("%1.%2").arg(path, iterator.key());
      if (unsafe_style_reference_payload_key(iterator.key())) {
        errors->append(
            QStringLiteral("Style profile references must store metadata, paths, or recipe summaries, not embedded image payloads: %1")
                .arg(child_path));
      }
      append_unsafe_reference_payload_errors(iterator.value(), child_path, errors);
    }
    return;
  }
  if (value.isArray()) {
    const auto array = value.toArray();
    for (int index = 0; index < array.size(); ++index) {
      append_unsafe_reference_payload_errors(
          array.at(index),
          QStringLiteral("%1[%2]").arg(path).arg(index),
          errors);
    }
  }
}

QJsonObject allowed_tools(
    bool color_grading,
    const QString& crop,
    bool masks,
    const QString& retouch,
    bool layers_luts) {
  return QJsonObject{
      {QStringLiteral("raw"), true},
      {QStringLiteral("global_color"), true},
      {QStringLiteral("curves"), true},
      {QStringLiteral("hsl"), true},
      {QStringLiteral("color_grading"), color_grading},
      {QStringLiteral("detail"), true},
      {QStringLiteral("crop"), crop},
      {QStringLiteral("masks"), masks},
      {QStringLiteral("retouch"), retouch},
      {QStringLiteral("layers_luts"), layers_luts},
      {QStringLiteral("graphics"), true},
      {QStringLiteral("export"), false},
  };
}

QJsonObject constraints(
    bool preserve_skin_texture,
    bool avoid_clipped_highlights,
    bool avoid_crushed_blacks,
    double max_exposure_shift,
    int max_temperature_shift,
    int max_saturation_shift,
    const QJsonObject& color_constraints = {}) {
  return QJsonObject{
      {QStringLiteral("preserve_skin_texture"), preserve_skin_texture},
      {QStringLiteral("avoid_clipped_highlights"), avoid_clipped_highlights},
      {QStringLiteral("avoid_crushed_blacks"), avoid_crushed_blacks},
      {QStringLiteral("max_exposure_shift"), max_exposure_shift},
      {QStringLiteral("max_temperature_shift"), max_temperature_shift},
      {QStringLiteral("max_saturation_shift"), max_saturation_shift},
      {QStringLiteral("color_constraints"),
       color_constraints.isEmpty()
           ? QJsonObject{
                 {QStringLiteral("preferred_temperature_bias"), QStringLiteral("neutral")},
                 {QStringLiteral("protected_hues"), QJsonArray{}},
                 {QStringLiteral("max_hsl_channel_shift"), 12},
                 {QStringLiteral("max_color_grading_saturation"), 10},
                 {QStringLiteral("allow_harmony"), false},
             }
           : color_constraints},
  };
}

QJsonObject color_constraints(
    const QString& preferred_temperature_bias,
    const QJsonArray& protected_hues,
    int max_hsl_channel_shift,
    int max_color_grading_saturation,
    bool allow_harmony) {
  return QJsonObject{
      {QStringLiteral("preferred_temperature_bias"), preferred_temperature_bias},
      {QStringLiteral("protected_hues"), protected_hues},
      {QStringLiteral("max_hsl_channel_shift"), max_hsl_channel_shift},
      {QStringLiteral("max_color_grading_saturation"), max_color_grading_saturation},
      {QStringLiteral("allow_harmony"), allow_harmony},
  };
}

QJsonObject validation(
    double highlight_clip_limit_percent,
    double shadow_clip_limit_percent,
    bool require_skin_tone_check,
    bool require_neutral_color_check = false) {
  return QJsonObject{
      {QStringLiteral("highlight_clip_limit_percent"), highlight_clip_limit_percent},
      {QStringLiteral("shadow_clip_limit_percent"), shadow_clip_limit_percent},
      {QStringLiteral("require_skin_tone_check"), require_skin_tone_check},
      {QStringLiteral("require_neutral_color_check"), require_neutral_color_check},
      {QStringLiteral("require_before_after_proof"), true},
  };
}

QJsonObject batch_behavior(
    bool cluster_by_lighting = true,
    bool maintain_sequence_consistency = true,
    bool correct_outliers_individually = true) {
  return QJsonObject{
      {QStringLiteral("cluster_by_lighting"), cluster_by_lighting},
      {QStringLiteral("maintain_sequence_consistency"), maintain_sequence_consistency},
      {QStringLiteral("correct_outliers_individually"), correct_outliers_individually},
  };
}

QJsonObject reference_policy() {
  return QJsonObject{
      {QStringLiteral("mode"), QStringLiteral("text_rulebook_only")},
      {QStringLiteral("reference_image_matching"), QStringLiteral("deferred")},
      {QStringLiteral("examples_supported"), false},
  };
}

QJsonObject style_object(
    const QString& id,
    const QString& name,
    const QString& goal,
    const QJsonObject& allowed_tools,
    const QJsonObject& constraints,
    const QJsonObject& validation,
    const QJsonObject& batch_behavior) {
  return QJsonObject{
      {QStringLiteral("schema_version"), kCurrentAgentEditStyleSchemaVersion},
      {QStringLiteral("id"), id},
      {QStringLiteral("name"), name},
      {QStringLiteral("goal"), goal},
      {QStringLiteral("allowed_tools"), allowed_tools},
      {QStringLiteral("constraints"), constraints},
      {QStringLiteral("validation"), validation},
      {QStringLiteral("batch_behavior"), batch_behavior},
      {QStringLiteral("reference_policy"), reference_policy()},
  };
}

QJsonObject natural_portrait_style() {
  return style_object(
      QStringLiteral("natural-portrait"),
      QStringLiteral("Natural Portrait"),
      QStringLiteral("Portrait edit with natural skin, gentle contrast, clean eyes, and restrained background separation."),
      allowed_tools(true, QStringLiteral("conservative"), true, QStringLiteral("minor_cleanup_only"), false),
      constraints(
          true,
          true,
          true,
          1.0,
          1400,
          14,
          color_constraints(
              QStringLiteral("neutral"),
              QJsonArray{QStringLiteral("skin")},
              10,
              8,
              false)),
      validation(0.5, 1.0, true),
      batch_behavior());
}

QJsonObject warm_editorial_wedding_style() {
  return style_object(
      QStringLiteral("warm-editorial-wedding"),
      QStringLiteral("Warm Editorial Wedding"),
      QStringLiteral("Natural warm editorial wedding edit with protected skin tones and soft highlight rolloff."),
      allowed_tools(true, QStringLiteral("conservative"), true, QStringLiteral("minor_cleanup_only"), false),
      constraints(
          true,
          true,
          true,
          1.2,
          1800,
          18,
          color_constraints(
              QStringLiteral("warm"),
              QJsonArray{QStringLiteral("skin"), QStringLiteral("neutral")},
              12,
              12,
              true)),
      validation(0.5, 1.0, true),
      batch_behavior());
}

QJsonObject product_true_color_style() {
  return style_object(
      QStringLiteral("product-true-color"),
      QStringLiteral("Product True Color"),
      QStringLiteral("Product edit with accurate color, clean whites, consistent background, sharp detail, and no hue drift."),
      allowed_tools(false, QStringLiteral("straighten_and_crop"), true, QStringLiteral("dust_cleanup_only"), false),
      constraints(
          false,
          true,
          true,
          0.8,
          900,
          10,
          color_constraints(
              QStringLiteral("neutral"),
              QJsonArray{QStringLiteral("product"), QStringLiteral("neutral")},
              6,
              0,
              false)),
      validation(0.25, 0.5, false, true),
      batch_behavior());
}

QJsonObject balanced_landscape_style() {
  return style_object(
      QStringLiteral("balanced-landscape"),
      QStringLiteral("Balanced Landscape"),
      QStringLiteral("Landscape edit with balanced dynamic range, believable greens and blues, crisp detail, and natural skies."),
      allowed_tools(true, QStringLiteral("composition_safe"), true, QStringLiteral("none"), false),
      constraints(
          false,
          true,
          true,
          1.4,
          2200,
          20,
          color_constraints(
              QStringLiteral("neutral"),
              QJsonArray{QStringLiteral("sky"), QStringLiteral("foliage")},
              16,
              14,
              true)),
      validation(0.4, 1.0, false),
      batch_behavior());
}

QJsonObject real_estate_bright_natural_style() {
  return style_object(
      QStringLiteral("real-estate-bright-natural"),
      QStringLiteral("Real Estate Bright Natural"),
      QStringLiteral("Real estate edit with bright rooms, neutral walls, straight verticals, clean windows, and believable exterior color."),
      allowed_tools(false, QStringLiteral("straighten_verticals"), true, QStringLiteral("minor_cleanup_only"), false),
      constraints(
          false,
          true,
          true,
          1.5,
          1600,
          12,
          color_constraints(
              QStringLiteral("neutral"),
              QJsonArray{QStringLiteral("neutral")},
              6,
              0,
              false)),
      validation(0.35, 0.75, false, true),
      batch_behavior());
}

QJsonObject street_contrast_style() {
  return style_object(
      QStringLiteral("street-contrast"),
      QStringLiteral("Street Contrast"),
      QStringLiteral("Street edit with decisive contrast, preserved atmosphere, controlled color cast, and protected highlight detail."),
      allowed_tools(true, QStringLiteral("composition_safe"), true, QStringLiteral("none"), false),
      constraints(
          false,
          true,
          true,
          1.1,
          1800,
          16,
          color_constraints(
              QStringLiteral("neutral"),
              QJsonArray{QStringLiteral("skin")},
              12,
              8,
              false)),
      validation(0.6, 1.5, false),
      batch_behavior());
}

QJsonObject event_clean_mixed_light_style() {
  return style_object(
      QStringLiteral("event-clean-mixed-light"),
      QStringLiteral("Event Clean Mixed Light"),
      QStringLiteral("Event edit that normalizes mixed lighting, protects faces, keeps sequence consistency, and avoids over-stylizing."),
      allowed_tools(true, QStringLiteral("conservative"), true, QStringLiteral("minor_cleanup_only"), false),
      constraints(
          true,
          true,
          true,
          1.2,
          2400,
          14,
          color_constraints(
              QStringLiteral("neutral"),
              QJsonArray{QStringLiteral("skin"), QStringLiteral("neutral")},
              8,
              6,
              false)),
      validation(0.5, 1.0, true),
      batch_behavior());
}

QJsonObject filmic_soft_contrast_style() {
  return style_object(
      QStringLiteral("filmic-soft-contrast"),
      QStringLiteral("Filmic Soft Contrast"),
      QStringLiteral("Filmic edit with soft contrast, gentle color separation, protected highlight rolloff, and restrained grain-like detail."),
      allowed_tools(true, QStringLiteral("conservative"), true, QStringLiteral("none"), true),
      constraints(
          false,
          true,
          true,
          1.0,
          2000,
          16,
          color_constraints(
              QStringLiteral("warm"),
              QJsonArray{QStringLiteral("skin")},
              10,
              10,
              true)),
      validation(0.35, 0.75, false),
      batch_behavior());
}

QJsonObject clean_commercial_style() {
  return style_object(
      QStringLiteral("clean-commercial"),
      QStringLiteral("Clean Commercial"),
      QStringLiteral("Clean accurate edit with neutral color, controlled contrast, crisp detail, and no stylized color cast."),
      allowed_tools(false, QStringLiteral("conservative"), true, QStringLiteral("minor_cleanup_only"), false),
      constraints(
          false,
          true,
          true,
          1.0,
          1000,
          12,
          color_constraints(
              QStringLiteral("neutral"),
              QJsonArray{QStringLiteral("product"), QStringLiteral("neutral")},
              5,
              0,
              false)),
      validation(0.25, 0.75, false, true),
      batch_behavior());
}

QJsonObject black_and_white_tonal_style() {
  return style_object(
      QStringLiteral("black-and-white-tonal"),
      QStringLiteral("Black And White Tonal"),
      QStringLiteral("Black-and-white edit with intentional tonal separation, protected skin luminance, rich midtones, and clean highlight detail."),
      allowed_tools(false, QStringLiteral("conservative"), true, QStringLiteral("minor_cleanup_only"), false),
      constraints(
          true,
          true,
          true,
          1.1,
          0,
          0,
          color_constraints(
              QStringLiteral("neutral"),
              QJsonArray{QStringLiteral("skin")},
              0,
              0,
              false)),
      validation(0.35, 1.0, true),
      batch_behavior());
}

QJsonObject high_key_airy_style() {
  return style_object(
      QStringLiteral("high-key-airy"),
      QStringLiteral("High Key Airy"),
      QStringLiteral("High-key edit with luminous exposure, soft contrast, clean whites, and protected skin detail without clipping."),
      allowed_tools(true, QStringLiteral("conservative"), true, QStringLiteral("minor_cleanup_only"), false),
      constraints(
          true,
          true,
          false,
          1.3,
          1300,
          10,
          color_constraints(
              QStringLiteral("neutral"),
              QJsonArray{QStringLiteral("skin"), QStringLiteral("neutral")},
              8,
              4,
              false)),
      validation(0.2, 0.5, true),
      batch_behavior());
}

}  // namespace

AgentEditStyleValidationResult validate_agent_edit_style_object(
    const QJsonObject& object) {
  AgentEditStyleValidationResult result;
  append_executable_style_payload_errors(object, QStringLiteral("style"), &result.errors);
  append_unsafe_reference_payload_errors(object, QStringLiteral("style"), &result.errors);

  const auto schema_value = object.value(QStringLiteral("schema_version"));
  const auto schema_number = schema_value.toDouble(-1.0);
  const auto schema_version = static_cast<int>(schema_number);
  if (!schema_value.isDouble() || schema_number != static_cast<double>(schema_version) ||
      schema_version < 1) {
    result.errors.append(QStringLiteral("schema_version must be a positive integer"));
  } else if (schema_version > kCurrentAgentEditStyleSchemaVersion) {
    result.errors.append(
        QStringLiteral("schema_version %1 is newer than supported version %2")
            .arg(schema_version)
            .arg(kCurrentAgentEditStyleSchemaVersion));
  }

  if (!is_non_empty_string(object, QStringLiteral("id"))) {
    result.errors.append(QStringLiteral("id must be a non-empty string"));
  } else {
    static const QRegularExpression id_pattern(QStringLiteral("^[a-z0-9][a-z0-9_-]{1,62}$"));
    if (!id_pattern.match(trimmed_string(object, QStringLiteral("id"))).hasMatch()) {
      result.errors.append(QStringLiteral("id must use lowercase letters, digits, hyphens, or underscores"));
    }
  }
  if (!is_non_empty_string(object, QStringLiteral("name"))) {
    result.errors.append(QStringLiteral("name must be a non-empty string"));
  }
  if (!is_non_empty_string(object, QStringLiteral("goal"))) {
    result.errors.append(QStringLiteral("goal must be a non-empty string"));
  }

  require_object(object, QStringLiteral("allowed_tools"), &result.errors);
  require_object(object, QStringLiteral("constraints"), &result.errors);
  require_object(object, QStringLiteral("validation"), &result.errors);
  require_object(object, QStringLiteral("batch_behavior"), &result.errors);
  if (object.contains(QStringLiteral("reference_policy")) &&
      !object.value(QStringLiteral("reference_policy")).isObject()) {
    result.errors.append(QStringLiteral("reference_policy must be an object"));
  }
  if (object.contains(QStringLiteral("learned_profile")) &&
      !object.value(QStringLiteral("learned_profile")).isObject()) {
    result.errors.append(QStringLiteral("learned_profile must be an object"));
  }
  if (object.contains(QStringLiteral("tool_knowledge")) &&
      !object.value(QStringLiteral("tool_knowledge")).isObject()) {
    result.errors.append(QStringLiteral("tool_knowledge must be an object"));
  }
  if (object.contains(QStringLiteral("quality_model")) &&
      !object.value(QStringLiteral("quality_model")).isObject()) {
    result.errors.append(QStringLiteral("quality_model must be an object"));
  }
  if (object.contains(QStringLiteral("project_workflow")) &&
      !object.value(QStringLiteral("project_workflow")).isObject()) {
    result.errors.append(QStringLiteral("project_workflow must be an object"));
  }

  const auto allowed_tools = object.value(QStringLiteral("allowed_tools")).toObject();
  const QStringList known_tools = {
      QStringLiteral("raw"),
      QStringLiteral("global_color"),
      QStringLiteral("curves"),
      QStringLiteral("hsl"),
      QStringLiteral("color_grading"),
      QStringLiteral("detail"),
      QStringLiteral("crop"),
      QStringLiteral("masks"),
      QStringLiteral("retouch"),
      QStringLiteral("layers_luts"),
      QStringLiteral("graphics"),
      QStringLiteral("export"),
  };
  for (const auto& tool : known_tools) {
    if (!allowed_tools.contains(tool)) {
      result.warnings.append(QStringLiteral("allowed_tools.%1 is not set").arg(tool));
    }
  }
  if (!tool_enabled(allowed_tools, QStringLiteral("global_color"))) {
    result.warnings.append(QStringLiteral("style cannot perform useful first-release edits without global_color"));
  }

  const auto constraints = object.value(QStringLiteral("constraints")).toObject();
  if (constraints.contains(QStringLiteral("color_constraints"))) {
    const auto color_constraints_value = constraints.value(QStringLiteral("color_constraints"));
    if (!color_constraints_value.isObject()) {
      result.errors.append(QStringLiteral("constraints.color_constraints must be an object"));
    } else {
      const auto color_constraints = color_constraints_value.toObject();
      const auto preferred_temperature_bias =
          color_constraints.value(QStringLiteral("preferred_temperature_bias"))
              .toString(QStringLiteral("neutral"));
      if (preferred_temperature_bias != QStringLiteral("neutral") &&
          preferred_temperature_bias != QStringLiteral("warm") &&
          preferred_temperature_bias != QStringLiteral("cool")) {
        result.errors.append(
            QStringLiteral("constraints.color_constraints.preferred_temperature_bias must be neutral, warm, or cool"));
      }

      const auto protected_hues = color_constraints.value(QStringLiteral("protected_hues"));
      if (protected_hues.isArray()) {
        const QStringList known_hues{
            QStringLiteral("skin"),
            QStringLiteral("sky"),
            QStringLiteral("foliage"),
            QStringLiteral("product"),
            QStringLiteral("neutral"),
        };
        for (const auto& hue_value : protected_hues.toArray()) {
          const auto hue = hue_value.toString();
          if (!known_hues.contains(hue)) {
            result.errors.append(
                QStringLiteral("constraints.color_constraints.protected_hues contains unsupported hue: %1")
                    .arg(hue));
          }
        }
      } else if (!protected_hues.isUndefined()) {
        result.errors.append(QStringLiteral("constraints.color_constraints.protected_hues must be an array"));
      }

      const auto max_hsl_channel_shift =
          color_constraints.value(QStringLiteral("max_hsl_channel_shift"));
      if (max_hsl_channel_shift.isDouble() &&
          (max_hsl_channel_shift.toDouble() < 0.0 || max_hsl_channel_shift.toDouble() > 60.0)) {
        result.errors.append(
            QStringLiteral("constraints.color_constraints.max_hsl_channel_shift must be between 0 and 60"));
      }
      const auto max_color_grading_saturation =
          color_constraints.value(QStringLiteral("max_color_grading_saturation"));
      if (max_color_grading_saturation.isDouble() &&
          (max_color_grading_saturation.toDouble() < 0.0 ||
           max_color_grading_saturation.toDouble() > 60.0)) {
        result.errors.append(
            QStringLiteral("constraints.color_constraints.max_color_grading_saturation must be between 0 and 60"));
      }
      const auto allow_harmony = color_constraints.value(QStringLiteral("allow_harmony"));
      if (!allow_harmony.isUndefined() && !allow_harmony.isBool()) {
        result.errors.append(QStringLiteral("constraints.color_constraints.allow_harmony must be a boolean"));
      }
    }
  }

  const auto validation = object.value(QStringLiteral("validation")).toObject();
  const auto highlight_limit = validation.value(QStringLiteral("highlight_clip_limit_percent"));
  if (highlight_limit.isDouble() &&
      (highlight_limit.toDouble() < 0.0 || highlight_limit.toDouble() > 10.0)) {
    result.errors.append(QStringLiteral("validation.highlight_clip_limit_percent must be between 0 and 10"));
  }
  const auto shadow_limit = validation.value(QStringLiteral("shadow_clip_limit_percent"));
  if (shadow_limit.isDouble() &&
      (shadow_limit.toDouble() < 0.0 || shadow_limit.toDouble() > 10.0)) {
    result.errors.append(QStringLiteral("validation.shadow_clip_limit_percent must be between 0 and 10"));
  }
  const auto reference_policy = object.value(QStringLiteral("reference_policy")).toObject();
  if (!reference_policy.isEmpty()) {
    const auto mode = reference_policy.value(QStringLiteral("mode")).toString();
    const auto reference_image_matching =
        reference_policy.value(QStringLiteral("reference_image_matching")).toString();
    const auto learned_profile_mode = mode == QStringLiteral("learned_style_profile");
    if (mode != QStringLiteral("text_rulebook_only") && !learned_profile_mode) {
      result.errors.append(QStringLiteral("reference_policy.mode must be text_rulebook_only or learned_style_profile for schema v1"));
    }
    if (learned_profile_mode) {
      if (reference_image_matching != QStringLiteral("metadata_only") &&
          reference_image_matching != QStringLiteral("rendered_proof_and_recipe")) {
        result.errors.append(QStringLiteral("reference_policy.reference_image_matching must be metadata_only or rendered_proof_and_recipe for learned style profiles"));
      }
      if (!reference_policy.value(QStringLiteral("examples_supported")).toBool()) {
        result.warnings.append(QStringLiteral("learned style profiles should set reference_policy.examples_supported to true"));
      }
    } else if (reference_image_matching != QStringLiteral("deferred")) {
      result.errors.append(QStringLiteral("reference_policy.reference_image_matching must be deferred for text-rulebook styles"));
    }
    if (!learned_profile_mode && reference_policy.value(QStringLiteral("examples_supported")).toBool()) {
      result.errors.append(QStringLiteral("reference_policy.examples_supported must be false for schema v1"));
    }
  }

  const auto learned_profile = object.value(QStringLiteral("learned_profile")).toObject();
  if (!learned_profile.isEmpty()) {
    const auto source_asset_count = learned_profile.value(QStringLiteral("source_asset_count"));
    if (source_asset_count.isDouble() && source_asset_count.toInt() < 1) {
      result.errors.append(QStringLiteral("learned_profile.source_asset_count must be at least 1"));
    }
    const auto references = learned_profile.value(QStringLiteral("reference_examples"));
    if (references.isArray() && references.toArray().size() > 200) {
      result.errors.append(QStringLiteral("learned_profile.reference_examples supports at most 200 entries"));
    } else if (!references.isUndefined() && !references.isArray()) {
      result.errors.append(QStringLiteral("learned_profile.reference_examples must be an array"));
    }
  }

  result.valid = result.errors.isEmpty();
  return result;
}

AgentEditStyleValidationResult validate_agent_edit_style_json(
    const QString& style_json) {
  AgentEditStyleValidationResult result;
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(style_json.toUtf8(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError) {
    result.errors.append(
        QStringLiteral("style JSON parse error at offset %1: %2")
            .arg(parse_error.offset)
            .arg(parse_error.errorString()));
    return result;
  }
  if (!document.isObject()) {
    result.errors.append(QStringLiteral("style root must be a JSON object"));
    return result;
  }
  return validate_agent_edit_style_object(document.object());
}

AgentEditStyle parse_agent_edit_style_object(
    const QJsonObject& object,
    const QString& source) {
  AgentEditStyle style;
  style.id = trimmed_string(object, QStringLiteral("id"));
  style.name = trimmed_string(object, QStringLiteral("name"));
  style.goal = trimmed_string(object, QStringLiteral("goal"));
  style.allowed_tools = object_map(object, QStringLiteral("allowed_tools"));
  style.constraints = object_map(object, QStringLiteral("constraints"));
  style.validation = object_map(object, QStringLiteral("validation"));
  style.batch_behavior = object_map(object, QStringLiteral("batch_behavior"));
  style.reference_policy = object.contains(QStringLiteral("reference_policy"))
                               ? object_map(object, QStringLiteral("reference_policy"))
                               : reference_policy().toVariantMap();
  style.learned_profile = object_map(object, QStringLiteral("learned_profile"));
  style.tool_knowledge = object_map(object, QStringLiteral("tool_knowledge"));
  style.quality_model = object_map(object, QStringLiteral("quality_model"));
  style.project_workflow = object_map(object, QStringLiteral("project_workflow"));
  style.source = source;
  return style;
}

QVariantMap agent_edit_style_map(const AgentEditStyle& style) {
  QVariantMap map;
  map.insert(QStringLiteral("schemaVersion"), kCurrentAgentEditStyleSchemaVersion);
  map.insert(QStringLiteral("id"), style.id);
  map.insert(QStringLiteral("name"), style.name);
  map.insert(QStringLiteral("goal"), style.goal);
  map.insert(QStringLiteral("allowedTools"), style.allowed_tools);
  map.insert(QStringLiteral("constraints"), style.constraints);
  map.insert(QStringLiteral("validation"), style.validation);
  map.insert(QStringLiteral("batchBehavior"), style.batch_behavior);
  map.insert(QStringLiteral("referencePolicy"), style.reference_policy);
  map.insert(QStringLiteral("learnedProfile"), style.learned_profile);
  map.insert(QStringLiteral("toolKnowledge"), style.tool_knowledge);
  map.insert(QStringLiteral("qualityModel"), style.quality_model);
  map.insert(QStringLiteral("projectWorkflow"), style.project_workflow);
  map.insert(QStringLiteral("source"), style.source);
  return map;
}

QVariantList built_in_agent_edit_styles() {
  QVariantList styles;
  const QVector<QJsonObject> built_ins = {
      natural_portrait_style(),
      warm_editorial_wedding_style(),
      product_true_color_style(),
      balanced_landscape_style(),
      real_estate_bright_natural_style(),
      street_contrast_style(),
      event_clean_mixed_light_style(),
      filmic_soft_contrast_style(),
      clean_commercial_style(),
      black_and_white_tonal_style(),
      high_key_airy_style(),
  };
  for (const auto& object : built_ins) {
    styles.append(agent_edit_style_map(
        parse_agent_edit_style_object(object, QStringLiteral("built-in"))));
  }
  return styles;
}

QVariantList load_agent_edit_styles_from_directory(
    const QString& directory_path,
    QStringList* errors) {
  QVariantList styles;
  const QDir directory(directory_path);
  if (!directory.exists()) {
    return styles;
  }

  const auto entries = directory.entryInfoList(
      {QStringLiteral("*.json")},
      QDir::Files | QDir::Readable,
      QDir::Name);
  for (const auto& entry : entries) {
    QFile file(entry.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      if (errors != nullptr) {
        errors->append(QStringLiteral("%1: %2").arg(entry.fileName(), file.errorString()));
      }
      continue;
    }
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
      if (errors != nullptr) {
        errors->append(
            QStringLiteral("%1: invalid style JSON").arg(entry.fileName()));
      }
      continue;
    }
    const auto validation = validate_agent_edit_style_object(document.object());
    if (!validation.valid) {
      if (errors != nullptr) {
        errors->append(
            QStringLiteral("%1: %2")
                .arg(entry.fileName(), validation.errors.join(QStringLiteral("; "))));
      }
      continue;
    }
    styles.append(agent_edit_style_map(
        parse_agent_edit_style_object(document.object(), entry.absoluteFilePath())));
  }
  return styles;
}

bool save_agent_edit_style_to_directory(
    const QString& directory_path,
    const QVariantMap& style,
    QString* error_message) {
  QDir directory(directory_path);
  if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to create style directory.");
    }
    return false;
  }

  const auto object = QJsonObject::fromVariantMap(style);
  const auto validation = validate_agent_edit_style_object(object);
  if (!validation.valid) {
    if (error_message != nullptr) {
      *error_message = validation.errors.join(QStringLiteral("; "));
    }
    return false;
  }

  const auto id = object.value(QStringLiteral("id")).toString().trimmed();
  QSaveFile file(directory.filePath(id + QStringLiteral(".json")));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (error_message != nullptr) {
      *error_message = file.errorString();
    }
    return false;
  }
  file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
  if (!file.commit()) {
    if (error_message != nullptr) {
      *error_message = file.errorString();
    }
    return false;
  }
  return true;
}

}  // namespace photo_editor::desktop
