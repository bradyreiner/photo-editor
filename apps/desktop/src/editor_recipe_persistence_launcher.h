#pragma once

#include "app_bootstrap.h"
#include "catalog_helper_payload.h"
#include "decoder_provenance.h"
#include "desktop_recipe_persistence_service.h"
#include "rendering_migration_evidence.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>

namespace photo_editor::desktop {

[[nodiscard]] inline bool start_editor_recipe_persistence(
    QProcess* process,
    const QString& asset_id,
    const EditRecipeState& state,
    const DecoderProvenanceResult& provenance,
    const QString& app_data_root,
    const QString& working_directory,
    QString* error) {
  if (process == nullptr) {
    if (error != nullptr) {
      *error =
          QStringLiteral("Edit recipe persistence process is unavailable.");
    }
    return false;
  }
  const auto persistence =
      prepare_desktop_recipe_persistence(
          asset_id,
          state,
          provenance,
          QDir(app_data_root)
              .filePath(QStringLiteral("recipe-assets")));
  if (!persistence.ok()) {
    if (error != nullptr) {
      *error = persistence.error;
    }
    return false;
  }
  QProcessEnvironment environment;
  QString environment_error;
  if (!AppBootstrap::prepareProcessEnvironment(
          &environment,
          &environment_error)) {
    if (error != nullptr) {
      *error =
          environment_error.isEmpty()
              ? QStringLiteral("Edit recipe save is unavailable.")
              : environment_error;
    }
    return false;
  }
  process->setWorkingDirectory(working_directory);
  process->setProcessEnvironment(environment);
  process->setProperty("recipeMigrationAcceptance", false);
  if (persistence.legacy_only) {
    return start_catalog_helper_stdin(process,
        AppBootstrap::helperProgram(QStringLiteral("save_edit_recipe")),
        AppBootstrap::helperArguments(QStringLiteral("save_edit_recipe"),
            {QStringLiteral("--stdin-json-v1")}),
        {QStringLiteral("--legacy-v1"), asset_id, persistence.legacy_recipe_json}, error);
  }
  return start_catalog_helper_stdin(process,
      AppBootstrap::helperProgram(
          QStringLiteral("save_edit_recipe")),
      AppBootstrap::helperArguments(
          QStringLiteral("save_edit_recipe"),
          {QStringLiteral("--stdin-json-v1")}),
          {
              asset_id,
              persistence.legacy_recipe_json,
              persistence.canonical_document.recipe_payload,
              persistence.canonical_document.cache_key,
              persistence.canonical_document.provenance_payload,
          }, error);
}

[[nodiscard]] inline bool start_editor_recipe_migration_acceptance(
    QProcess* process,
    const QString& asset_id,
    const EditRecipeState& state,
    const DecoderProvenanceResult& provenance,
    const QVariantMap& evidence,
    const QString& app_data_root,
    const QString& working_directory,
    QString* error) {
  if (process == nullptr) {
    if (error != nullptr) {
      *error = QStringLiteral("Recipe migration process is unavailable.");
    }
    return false;
  }
  const auto persistence = prepare_desktop_recipe_persistence(
      asset_id,
      state,
      provenance,
      QDir(app_data_root).filePath(QStringLiteral("recipe-assets")));
  if (!persistence.ok()) {
    if (error != nullptr) {
      *error = persistence.error;
    }
    return false;
  }
  if (persistence.legacy_only) {
    if (error) *error = QStringLiteral("Rendering migration requires a canonical target recipe.");
    return false;
  }
  const auto profile_identities =
      canonical_v13_profile_identities(
          persistence.canonical_document);
  QJsonArray profiles;
  for (const auto& identity : profile_identities) {
    profiles.append(identity);
  }
  const auto plan_hash =
      evidence.value(QStringLiteral("planCacheKey")).toString();
  const auto evidence_hash =
      evidence.value(QStringLiteral("evidenceSha256")).toString();
  const auto source_renderer =
      evidence.value(QStringLiteral("referenceModel")).toString();
  const auto target_renderer =
      evidence.value(QStringLiteral("candidateModel")).toString();
  const auto approval_time =
      evidence.value(QStringLiteral("approvalTime")).toString();
  if (plan_hash.isEmpty() || evidence_hash.size() != 64 ||
      source_renderer.isEmpty() || target_renderer.isEmpty() ||
      approval_time.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("Measured migration evidence is incomplete.");
    }
    return false;
  }
  QProcessEnvironment environment;
  QString environment_error;
  if (!AppBootstrap::prepareProcessEnvironment(
          &environment,
          &environment_error)) {
    if (error != nullptr) {
      *error = environment_error.isEmpty()
                   ? QStringLiteral("Recipe migration is unavailable.")
                   : environment_error;
    }
    return false;
  }
  process->setWorkingDirectory(working_directory);
  process->setProcessEnvironment(environment);
  process->setProperty("recipeMigrationAcceptance", true);
  return start_catalog_helper_stdin(process,
      AppBootstrap::helperProgram(
          QStringLiteral("accept_recipe_migration")),
      AppBootstrap::helperArguments(
          QStringLiteral("accept_recipe_migration"),
          {QStringLiteral("--stdin-json-v1")}),
          {
              asset_id,
              persistence.legacy_recipe_json,
              persistence.canonical_document.recipe_payload,
              persistence.canonical_document.cache_key,
              persistence.canonical_document.provenance_payload,
              QStringLiteral("12"),
              source_renderer,
              target_renderer,
              QString::fromUtf8(
                  QJsonDocument(profiles).toJson(QJsonDocument::Compact)),
              plan_hash,
              evidence_hash,
              approval_time,
              QString::fromUtf8(
                  canonical_rendering_migration_evidence_payload(
                      evidence)),
          }, error);
}

}  // namespace photo_editor::desktop
