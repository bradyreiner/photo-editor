#include <cstdlib>
#include <iostream>

#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QIODevice>
#include <QSet>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include "agent_edit_style.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QVariantMap valid_style() {
  QVariantMap style;
  style.insert(QStringLiteral("schema_version"), photo_editor::desktop::kCurrentAgentEditStyleSchemaVersion);
  style.insert(QStringLiteral("id"), QStringLiteral("soft-clean-test"));
  style.insert(QStringLiteral("name"), QStringLiteral("Soft Clean Test"));
  style.insert(
      QStringLiteral("goal"),
      QStringLiteral("Clean natural edit with gentle contrast and protected highlights."));
  style.insert(
      QStringLiteral("allowed_tools"),
      QVariantMap{
          {QStringLiteral("raw"), true},
          {QStringLiteral("global_color"), true},
          {QStringLiteral("curves"), true},
          {QStringLiteral("hsl"), true},
          {QStringLiteral("color_grading"), false},
          {QStringLiteral("detail"), true},
          {QStringLiteral("crop"), QStringLiteral("conservative")},
          {QStringLiteral("masks"), true},
          {QStringLiteral("retouch"), QStringLiteral("minor_cleanup_only")},
          {QStringLiteral("layers_luts"), false},
          {QStringLiteral("graphics"), true},
          {QStringLiteral("export"), false},
      });
  style.insert(
      QStringLiteral("constraints"),
      QVariantMap{
          {QStringLiteral("avoid_clipped_highlights"), true},
          {QStringLiteral("avoid_crushed_blacks"), true},
          {QStringLiteral("max_exposure_shift"), 1.0},
      });
  style.insert(
      QStringLiteral("validation"),
      QVariantMap{
          {QStringLiteral("highlight_clip_limit_percent"), 0.5},
          {QStringLiteral("shadow_clip_limit_percent"), 1.0},
          {QStringLiteral("require_before_after_proof"), true},
      });
  style.insert(
      QStringLiteral("batch_behavior"),
      QVariantMap{
          {QStringLiteral("cluster_by_lighting"), true},
          {QStringLiteral("maintain_sequence_consistency"), true},
          {QStringLiteral("correct_outliers_individually"), true},
      });
  style.insert(
      QStringLiteral("reference_policy"),
      QVariantMap{
          {QStringLiteral("mode"), QStringLiteral("text_rulebook_only")},
          {QStringLiteral("reference_image_matching"), QStringLiteral("deferred")},
          {QStringLiteral("examples_supported"), false},
      });
  return style;
}

QJsonObject style_object_from_public_map(const QVariantMap& style) {
  return QJsonObject::fromVariantMap(
      QVariantMap{
          {QStringLiteral("schema_version"), style.value(QStringLiteral("schemaVersion"))},
          {QStringLiteral("id"), style.value(QStringLiteral("id"))},
          {QStringLiteral("name"), style.value(QStringLiteral("name"))},
          {QStringLiteral("goal"), style.value(QStringLiteral("goal"))},
          {QStringLiteral("allowed_tools"), style.value(QStringLiteral("allowedTools"))},
          {QStringLiteral("constraints"), style.value(QStringLiteral("constraints"))},
          {QStringLiteral("validation"), style.value(QStringLiteral("validation"))},
          {QStringLiteral("batch_behavior"), style.value(QStringLiteral("batchBehavior"))},
          {QStringLiteral("reference_policy"), style.value(QStringLiteral("referencePolicy"))},
          {QStringLiteral("learned_profile"), style.value(QStringLiteral("learnedProfile"))},
          {QStringLiteral("tool_knowledge"), style.value(QStringLiteral("toolKnowledge"))},
          {QStringLiteral("quality_model"), style.value(QStringLiteral("qualityModel"))},
          {QStringLiteral("project_workflow"), style.value(QStringLiteral("projectWorkflow"))},
      });
}

}  // namespace

int main() {
  const auto built_ins = photo_editor::desktop::built_in_agent_edit_styles();
  const QSet<QString> expected_built_in_ids = {
      QStringLiteral("natural-portrait"),
      QStringLiteral("warm-editorial-wedding"),
      QStringLiteral("product-true-color"),
      QStringLiteral("balanced-landscape"),
      QStringLiteral("real-estate-bright-natural"),
      QStringLiteral("street-contrast"),
      QStringLiteral("event-clean-mixed-light"),
      QStringLiteral("filmic-soft-contrast"),
      QStringLiteral("clean-commercial"),
      QStringLiteral("black-and-white-tonal"),
      QStringLiteral("high-key-airy"),
  };
  if (built_ins.size() < expected_built_in_ids.size()) {
    return fail("agent editing should ship with built-in styles for each first-release genre goal");
  }
  QSet<QString> seen_built_in_ids;
  for (const auto& built_in_value : built_ins) {
    const auto built_in = built_in_value.toMap();
    const auto id = built_in.value(QStringLiteral("id")).toString();
    seen_built_in_ids.insert(id);
    if (id.isEmpty() ||
        built_in.value(QStringLiteral("name")).toString().isEmpty() ||
        built_in.value(QStringLiteral("goal")).toString().isEmpty() ||
        built_in.value(QStringLiteral("allowedTools")).toMap().isEmpty() ||
        !built_in.value(QStringLiteral("allowedTools")).toMap()
             .value(QStringLiteral("graphics")).toBool() ||
        built_in.value(QStringLiteral("constraints")).toMap().isEmpty() ||
        built_in.value(QStringLiteral("constraints"))
            .toMap()
            .value(QStringLiteral("color_constraints"))
            .toMap()
            .isEmpty() ||
        built_in.value(QStringLiteral("validation")).toMap().isEmpty() ||
        built_in.value(QStringLiteral("batchBehavior")).toMap().isEmpty() ||
        built_in.value(QStringLiteral("referencePolicy")).toMap().value(QStringLiteral("mode")).toString() !=
            QStringLiteral("text_rulebook_only")) {
      return fail("built-in styles should expose ids, goals, tools, constraints, validation, batch rules, and reference policy");
    }
    if (built_in.value(QStringLiteral("source")).toString() != QStringLiteral("built-in")) {
      return fail("built-in styles should report built-in source");
    }
    const auto built_in_validation =
        photo_editor::desktop::validate_agent_edit_style_object(style_object_from_public_map(built_in));
    if (!built_in_validation.valid || !built_in_validation.warnings.isEmpty()) {
      std::cerr << id.toStdString() << ": "
                << built_in_validation.errors.join(QStringLiteral("; ")).toStdString()
                << built_in_validation.warnings.join(QStringLiteral("; ")).toStdString()
                << '\n';
      return fail("built-in styles should pass schema validation without warnings");
    }
  }
  for (const auto& expected_id : expected_built_in_ids) {
    if (!seen_built_in_ids.contains(expected_id)) {
      std::cerr << expected_id.toStdString() << '\n';
      return fail("built-in style registry is missing a required genre style");
    }
  }

  auto style = valid_style();
  const auto style_object = QJsonObject::fromVariantMap(style);
  const auto validation = photo_editor::desktop::validate_agent_edit_style_object(style_object);
  if (!validation.valid) {
    std::cerr << validation.errors.join(QStringLiteral("; ")).toStdString() << '\n';
    return fail("valid agent edit style should pass schema validation");
  }

  auto invalid_style = style;
  invalid_style.insert(QStringLiteral("id"), QStringLiteral("../bad"));
  const auto invalid_validation =
      photo_editor::desktop::validate_agent_edit_style_object(QJsonObject::fromVariantMap(invalid_style));
  if (invalid_validation.valid ||
      !invalid_validation.errors.join(QStringLiteral("; ")).contains(QStringLiteral("id"))) {
    return fail("invalid style ids should be rejected");
  }

  auto executable_style = style;
  executable_style.insert(QStringLiteral("script"), QStringLiteral("rm -rf ~/Pictures"));
  const auto executable_validation =
      photo_editor::desktop::validate_agent_edit_style_object(QJsonObject::fromVariantMap(executable_style));
  if (executable_validation.valid ||
      !executable_validation.errors.join(QStringLiteral("; ")).contains(QStringLiteral("executable"))) {
    return fail("style validation should reject executable top-level payload fields");
  }

  auto nested_executable_style = style;
  auto nested_constraints = nested_executable_style.value(QStringLiteral("constraints")).toMap();
  nested_constraints.insert(QStringLiteral("onApply"), QStringLiteral("osascript do shell script"));
  nested_executable_style.insert(QStringLiteral("constraints"), nested_constraints);
  const auto nested_executable_validation =
      photo_editor::desktop::validate_agent_edit_style_object(QJsonObject::fromVariantMap(nested_executable_style));
  if (nested_executable_validation.valid ||
      !nested_executable_validation.errors.join(QStringLiteral("; ")).contains(QStringLiteral("constraints.onApply"))) {
    return fail("style validation should reject nested executable payload fields");
  }

  auto learned_reference_style = style;
  learned_reference_style.insert(
      QStringLiteral("reference_policy"),
      QVariantMap{
          {QStringLiteral("mode"), QStringLiteral("learned_style_profile")},
          {QStringLiteral("reference_image_matching"), QStringLiteral("rendered_proof_and_recipe")},
          {QStringLiteral("examples_supported"), true},
      });
  learned_reference_style.insert(
      QStringLiteral("learned_profile"),
      QVariantMap{
          {QStringLiteral("source_asset_count"), 2},
          {QStringLiteral("usable_recipe_count"), 2},
          {QStringLiteral("reference_examples"),
           QVariantList{
               QVariantMap{
                   {QStringLiteral("assetId"), QStringLiteral("asset-1")},
                   {QStringLiteral("filePath"), QStringLiteral("/tmp/look.jpg")},
                   {QStringLiteral("recipeSummary"),
                    QVariantMap{{QStringLiteral("exposure"), 0.2}}},
               },
           }},
      });
  learned_reference_style.insert(
      QStringLiteral("tool_knowledge"),
      QVariantMap{{QStringLiteral("globalTone"), QStringLiteral("Protect highlight detail.")}});
  learned_reference_style.insert(
      QStringLiteral("quality_model"),
      QVariantMap{{QStringLiteral("priorities"), QStringList{QStringLiteral("consistent style")}}});
  learned_reference_style.insert(
      QStringLiteral("project_workflow"),
      QVariantMap{{QStringLiteral("validateEveryImage"), true}});
  const auto learned_reference_validation =
      photo_editor::desktop::validate_agent_edit_style_object(QJsonObject::fromVariantMap(learned_reference_style));
  if (!learned_reference_validation.valid) {
    std::cerr << learned_reference_validation.errors.join(QStringLiteral("; ")).toStdString() << '\n';
    return fail("style validation should accept learned reference metadata for schema v1");
  }

  auto embedded_reference_style = learned_reference_style;
  embedded_reference_style.insert(
      QStringLiteral("reference_images"),
      QVariantList{
          QVariantMap{
              {QStringLiteral("path"), QStringLiteral("/tmp/look.jpg")},
              {QStringLiteral("dataUrl"), QStringLiteral("data:image/png;base64,abc")},
          },
      });
  const auto embedded_reference_validation =
      photo_editor::desktop::validate_agent_edit_style_object(QJsonObject::fromVariantMap(embedded_reference_style));
  if (embedded_reference_validation.valid ||
      !embedded_reference_validation.errors.join(QStringLiteral("; ")).contains(QStringLiteral("embedded image payloads"))) {
    return fail("style validation should reject embedded image payloads in learned style references");
  }

  auto unsupported_reference_policy_style = style;
  unsupported_reference_policy_style.insert(
      QStringLiteral("reference_policy"),
      QVariantMap{
          {QStringLiteral("mode"), QStringLiteral("image_reference_matching")},
          {QStringLiteral("reference_image_matching"), QStringLiteral("enabled")},
          {QStringLiteral("examples_supported"), true},
      });
  const auto unsupported_reference_policy_validation =
      photo_editor::desktop::validate_agent_edit_style_object(
          QJsonObject::fromVariantMap(unsupported_reference_policy_style));
  if (unsupported_reference_policy_validation.valid ||
      !unsupported_reference_policy_validation.errors.join(QStringLiteral("; ")).contains(QStringLiteral("reference_policy.mode"))) {
    return fail("style validation should constrain schema v1 reference matching modes");
  }

  auto invalid_color_constraints_style = style;
  auto invalid_color_constraints = invalid_color_constraints_style.value(QStringLiteral("constraints")).toMap();
  invalid_color_constraints.insert(
      QStringLiteral("color_constraints"),
      QVariantMap{
          {QStringLiteral("preferred_temperature_bias"), QStringLiteral("radioactive")},
          {QStringLiteral("protected_hues"), QVariantList{QStringLiteral("skin"), QStringLiteral("neon")}},
          {QStringLiteral("max_hsl_channel_shift"), 90.0},
          {QStringLiteral("max_color_grading_saturation"), -1.0},
          {QStringLiteral("allow_harmony"), QStringLiteral("yes")},
      });
  invalid_color_constraints_style.insert(QStringLiteral("constraints"), invalid_color_constraints);
  const auto invalid_color_constraints_validation =
      photo_editor::desktop::validate_agent_edit_style_object(
          QJsonObject::fromVariantMap(invalid_color_constraints_style));
  if (invalid_color_constraints_validation.valid ||
      !invalid_color_constraints_validation.errors.join(QStringLiteral("; "))
           .contains(QStringLiteral("constraints.color_constraints"))) {
    return fail("style validation should reject malformed color-specific constraints");
  }

  QTemporaryDir directory;
  if (!directory.isValid()) {
    return fail("temporary style directory should be created");
  }
  QString save_error;
  if (!photo_editor::desktop::save_agent_edit_style_to_directory(
          directory.path(),
          style,
          &save_error)) {
    std::cerr << save_error.toStdString() << '\n';
    return fail("valid agent edit style should save");
  }
  QStringList load_errors;
  const auto loaded = photo_editor::desktop::load_agent_edit_styles_from_directory(
      directory.path(),
      &load_errors);
  if (!load_errors.isEmpty()) {
    std::cerr << load_errors.join(QStringLiteral("; ")).toStdString() << '\n';
    return fail("saved style should reload without errors");
  }
  if (loaded.size() != 1 ||
      loaded.first().toMap().value(QStringLiteral("id")).toString() != QStringLiteral("soft-clean-test") ||
      loaded.first().toMap().value(QStringLiteral("source")).toString().isEmpty()) {
    return fail("loaded style should preserve id and source path");
  }

  QFile malformed(directory.filePath(QStringLiteral("malformed.json")));
  if (!malformed.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return fail("malformed style fixture should be writable");
  }
  malformed.write("{");
  malformed.close();
  load_errors.clear();
  const auto loaded_after_malformed =
      photo_editor::desktop::load_agent_edit_styles_from_directory(directory.path(), &load_errors);
  if (loaded_after_malformed.size() != 1 || load_errors.isEmpty()) {
    return fail("malformed user styles should be reported without hiding valid styles");
  }

  return EXIT_SUCCESS;
}
