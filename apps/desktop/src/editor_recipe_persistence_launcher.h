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

struct PreparedEditorRecipePersistence final {
  QByteArray payload;
  bool migration_acceptance = false;
  QString error;

  [[nodiscard]] bool ok() const noexcept {
    return error.isEmpty() && !payload.isEmpty();
  }
};

// Performs canonical serialization, evidence validation, and exact helper
// argument-envelope construction. Call this with an immutable snapshot on a
// worker; the returned bytes can be handed to QProcess without JSON work.
[[nodiscard]] inline PreparedEditorRecipePersistence
prepare_editor_recipe_persistence_payload(
    const QString& asset_id,
    const EditRecipeState& state,
    const DecoderProvenanceResult& provenance,
    const QVariantMap& evidence,
    const QString& app_data_root,
    bool migration_acceptance) {
  PreparedEditorRecipePersistence result;
  const auto persistence = prepare_desktop_recipe_persistence(
      asset_id,
      state,
      provenance,
      QDir(app_data_root).filePath(QStringLiteral("recipe-assets")));
  if (!persistence.ok()) {
    result.error = persistence.error;
    return result;
  }

  QStringList payload_arguments;
  if (!migration_acceptance) {
    if (persistence.legacy_only) {
      payload_arguments = {
          QStringLiteral("--legacy-v1"), asset_id,
          persistence.legacy_recipe_json};
    } else {
      payload_arguments = {
          asset_id,
          persistence.legacy_recipe_json,
          persistence.canonical_document.recipe_payload,
          persistence.canonical_document.cache_key,
          persistence.canonical_document.provenance_payload,
      };
    }
  } else {
    if (persistence.legacy_only) {
      result.error = QStringLiteral("Rendering migration requires a canonical target recipe.");
      return result;
    }
    const auto profile_identities =
        canonical_v13_profile_identities(persistence.canonical_document);
    QJsonArray profiles;
    for (const auto& identity : profile_identities) profiles.append(identity);
    const auto plan_hash = evidence.value(QStringLiteral("planCacheKey")).toString();
    const auto evidence_hash = evidence.value(QStringLiteral("evidenceSha256")).toString();
    const auto source_renderer = evidence.value(QStringLiteral("referenceModel")).toString();
    const auto target_renderer = evidence.value(QStringLiteral("candidateModel")).toString();
    const auto approval_time = evidence.value(QStringLiteral("approvalTime")).toString();
    if (plan_hash.isEmpty() || evidence_hash.size() != 64 ||
        source_renderer.isEmpty() || target_renderer.isEmpty() ||
        approval_time.isEmpty()) {
      result.error = QStringLiteral("Measured migration evidence is incomplete.");
      return result;
    }
    payload_arguments = {
        asset_id,
        persistence.legacy_recipe_json,
        persistence.canonical_document.recipe_payload,
        persistence.canonical_document.cache_key,
        persistence.canonical_document.provenance_payload,
        QStringLiteral("12"),
        source_renderer,
        target_renderer,
        QString::fromUtf8(QJsonDocument(profiles).toJson(QJsonDocument::Compact)),
        plan_hash,
        evidence_hash,
        approval_time,
        QString::fromUtf8(
            canonical_rendering_migration_evidence_payload(evidence)),
    };
  }

  result.payload = catalog_helper_argument_payload(payload_arguments);
  if (payload_arguments.size() > 32 ||
      result.payload.size() > kMaxCatalogHelperPayloadBytes) {
    result.payload.clear();
    result.error = QStringLiteral("Recipe payload exceeds the 256 MiB safety limit or has invalid arguments.");
    return result;
  }
  result.migration_acceptance = migration_acceptance;
  return result;
}

[[nodiscard]] inline bool start_prepared_editor_recipe_persistence(
    QProcess* process,
    const PreparedEditorRecipePersistence& prepared,
    const QString& working_directory,
    QString* error) {
  if (process == nullptr) {
    if (error != nullptr) {
      *error = prepared.migration_acceptance
                   ? QStringLiteral("Recipe migration process is unavailable.")
                   : QStringLiteral("Edit recipe persistence process is unavailable.");
    }
    return false;
  }
  if (!prepared.ok()) {
    if (error != nullptr) *error = prepared.error;
    return false;
  }
  QProcessEnvironment environment;
  QString environment_error;
  if (!AppBootstrap::prepareProcessEnvironment(&environment, &environment_error)) {
    if (error != nullptr) {
      *error = environment_error.isEmpty()
                   ? (prepared.migration_acceptance
                          ? QStringLiteral("Recipe migration is unavailable.")
                          : QStringLiteral("Edit recipe save is unavailable."))
                   : environment_error;
    }
    return false;
  }
  const auto helper_name = prepared.migration_acceptance
                               ? QStringLiteral("accept_recipe_migration")
                               : QStringLiteral("save_edit_recipe");
  process->setWorkingDirectory(working_directory);
  process->setProcessEnvironment(environment);
  process->setProperty("recipeMigrationAcceptance", prepared.migration_acceptance);
  return start_catalog_helper_stdin_payload(
      process,
      AppBootstrap::helperProgram(helper_name),
      AppBootstrap::helperArguments(helper_name, {QStringLiteral("--stdin-json-v1")}),
      prepared.payload,
      error);
}

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
  const auto prepared = prepare_editor_recipe_persistence_payload(
      asset_id, state, provenance, {}, app_data_root, false);
  return start_prepared_editor_recipe_persistence(
      process, prepared, working_directory, error);
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
  const auto prepared = prepare_editor_recipe_persistence_payload(
      asset_id, state, provenance, evidence, app_data_root, true);
  return start_prepared_editor_recipe_persistence(
      process, prepared, working_directory, error);
}

}  // namespace photo_editor::desktop
