#include <cstdlib>
#include <iostream>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

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

}  // namespace

int main() {
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
  qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");

  const auto warm_style = style_by_id(QStringLiteral("warm-editorial-wedding"));
  if (warm_style.isEmpty()) {
    return fail("warm built-in style should exist");
  }

  photo_editor::desktop::AgentLocalModelPlanRequest request;
  request.model_id = photo_editor::desktop::default_agent_local_model_id();
  request.style = warm_style;
  request.user_instructions = QStringLiteral("Keep it bright and soft but preserve highlights.");
  const auto plan = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (!plan.ok || plan.recipe_patch.isEmpty() || plan.plan_summary.isEmpty()) {
    return fail("deterministic local model should return a structured edit plan");
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

  request.model_id = QStringLiteral("missing-model");
  const auto missing_model_plan = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (missing_model_plan.ok || missing_model_plan.error_message.isEmpty()) {
    return fail("unsupported local model ids should be rejected");
  }

  return EXIT_SUCCESS;
}
