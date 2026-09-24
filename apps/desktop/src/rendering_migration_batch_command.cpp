#include "rendering_migration_batch_command.h"

#include "app_bootstrap.h"
#include "rendering_migration_asset_assessor.h"
#include "rendering_migration_batch_runner.h"
#include "rendering_migration_chunk_receipt.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>

#include <filesystem>
#include <system_error>

namespace photo_editor::desktop {
namespace {

[[nodiscard]] bool read_report(
    const QString& path,
    QVariantMap* report,
    QString* error) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    *error = QStringLiteral("migration component report could not be read: %1")
                 .arg(path);
    return false;
  }
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(
      file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError ||
      !document.isObject()) {
    *error = QStringLiteral("migration component report is invalid JSON: %1")
                 .arg(path);
    return false;
  }
  *report = document.object().toVariantMap();
  return true;
}

[[nodiscard]] QString sha256_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  QCryptographicHash digest(QCryptographicHash::Sha256);
  while (!file.atEnd()) {
    const auto block = file.read(1024 * 1024);
    if (block.isEmpty() && !file.atEnd()) {
      return {};
    }
    digest.addData(block);
  }
  return QString::fromLatin1(digest.result().toHex());
}

[[nodiscard]] std::filesystem::path filesystem_path(
    const QString& path) {
#if defined(Q_OS_WIN)
  return std::filesystem::path(path.toStdWString());
#else
  const auto encoded = QFile::encodeName(path);
  return std::filesystem::path(encoded.constData());
#endif
}

[[nodiscard]] bool materialize_artifact(
    const QString& source_path,
    const QString& destination_path,
    const QString& expected_hash,
    QString* error) {
  if (sha256_file(source_path) != expected_hash) {
    *error = QStringLiteral("migration component artifact identity is invalid: %1")
                 .arg(source_path);
    return false;
  }
  if (QFileInfo::exists(destination_path)) {
    if (sha256_file(destination_path) != expected_hash) {
      *error = QStringLiteral("merged migration artifact identity is invalid: %1")
                   .arg(destination_path);
      return false;
    }
    return true;
  }
  std::error_code link_error;
  std::filesystem::create_hard_link(
      filesystem_path(source_path),
      filesystem_path(destination_path),
      link_error);
  if (link_error && !QFile::copy(source_path, destination_path)) {
    *error = QStringLiteral("migration component artifact could not be materialized: %1")
                 .arg(source_path);
    return false;
  }
  if (sha256_file(destination_path) != expected_hash) {
    QFile::remove(destination_path);
    *error = QStringLiteral("materialized migration artifact identity is invalid: %1")
                 .arg(destination_path);
    return false;
  }
  return true;
}

}  // namespace

RenderingMigrationBatchCommandResult
run_packaged_rendering_migration_batch_command(
    const QString& report_path,
    const QString& recipe_asset_root,
    int inventory_timeout_ms,
    bool plan_only,
    const QString& chunk_authorization_path) {
  RenderingMigrationBatchCommandResult result;
  if (report_path.trimmed().isEmpty()) {
    result.error =
        QStringLiteral("migration report path is required");
    return result;
  }
  if (recipe_asset_root.trimmed().isEmpty()) {
    result.error =
        QStringLiteral("migration recipe asset root is required");
    return result;
  }
  if (inventory_timeout_ms <= 0) {
    result.error =
        QStringLiteral("migration inventory timeout is invalid");
    return result;
  }
  const auto executable_sha256 =
      sha256_file(QCoreApplication::applicationFilePath());
  const RenderingMigrationBatchProducer producer{
      .executable_sha256 = executable_sha256,
      .executable_name = QFileInfo(
          QCoreApplication::applicationFilePath()).fileName(),
#if defined(NDEBUG)
      .build_mode = QStringLiteral("release"),
#else
      .build_mode = QStringLiteral("debug"),
#endif
  };
  RenderingMigrationChunkAuthorizationResult authorization;
  if (plan_only) {
    if (!chunk_authorization_path.trimmed().isEmpty()) {
      result.error = QStringLiteral(
          "plan-only migration does not accept a chunk authorization");
      return result;
    }
  } else {
    if (chunk_authorization_path.trimmed().isEmpty()) {
      result.error = QStringLiteral(
          "rendering migration requires an approved chunk authorization");
      return result;
    }
    authorization = validate_rendering_migration_chunk_authorization(
        chunk_authorization_path, executable_sha256);
    if (!authorization.valid) {
      result.error = authorization.error;
      return result;
    }
    if (authorization.producer.value(QStringLiteral("executableName")).toString() !=
            producer.executable_name ||
        authorization.producer.value(QStringLiteral("buildMode")).toString() !=
            producer.build_mode) {
      result.error = QStringLiteral(
          "migration chunk authorization producer differs from this executable");
      return result;
    }
  }
  result.evidence_artifact_root =
      plan_only ? QString{} :
                  report_path + QStringLiteral(".assets");

  constexpr auto kInventoryHelper =
      "catalog_migration_inventory";
  result.inventory_program =
      AppBootstrap::helperProgram(
          QString::fromLatin1(kInventoryHelper));
  const auto inventory_arguments =
      AppBootstrap::helperArguments(
          QString::fromLatin1(kInventoryHelper), {});
  QProcessEnvironment environment;
  QString environment_error;
  if (!AppBootstrap::prepareProcessEnvironment(
          &environment, &environment_error)) {
    result.error =
        environment_error.isEmpty()
            ? QStringLiteral(
                  "migration inventory environment preparation failed")
            : environment_error;
    return result;
  }

  QProcess inventory_process;
  inventory_process.setProcessEnvironment(environment);
  inventory_process.setProcessChannelMode(
      QProcess::SeparateChannels);
  inventory_process.start(
      result.inventory_program,
      inventory_arguments,
      QIODevice::ReadOnly);
  if (!inventory_process.waitForStarted(10000)) {
    result.error = QStringLiteral(
                       "migration inventory helper did not start: %1")
                       .arg(inventory_process.errorString());
    return result;
  }
  if (!inventory_process.waitForFinished(
          inventory_timeout_ms)) {
    inventory_process.kill();
    inventory_process.waitForFinished(5000);
    result.error =
        QStringLiteral("migration inventory helper timed out");
    return result;
  }
  const auto standard_output =
      inventory_process.readAllStandardOutput();
  const auto standard_error =
      QString::fromUtf8(
          inventory_process.readAllStandardError())
          .trimmed();
  if (inventory_process.exitStatus() !=
          QProcess::NormalExit ||
      inventory_process.exitCode() != 0) {
    result.error = standard_error.isEmpty()
                       ? QStringLiteral(
                             "migration inventory helper failed with exit code %1")
                             .arg(inventory_process.exitCode())
                       : standard_error;
    return result;
  }

  QJsonParseError parse_error;
  const auto inventory_document =
      QJsonDocument::fromJson(
          standard_output, &parse_error);
  if (parse_error.error !=
          QJsonParseError::NoError ||
      !inventory_document.isObject()) {
    result.error = QStringLiteral(
                       "migration inventory helper returned invalid JSON: %1")
                       .arg(parse_error.errorString());
    return result;
  }

  const auto batch_result =
      run_rendering_migration_batch(
          inventory_document.object(),
          report_path,
          [&recipe_asset_root,
           plan_only,
           &result](
              const QVariantMap& inventory_asset) {
            if (plan_only) {
              return RenderingMigrationBatchAsset{
                  .asset_id = inventory_asset
                      .value(QStringLiteral("asset_id"))
                      .toString(),
                  .candidate_error = QStringLiteral(
                      "current executable evidence refresh required"),
              };
            }
            return assess_rendering_migration_inventory_asset(
                inventory_asset,
                recipe_asset_root,
                0,
                result.evidence_artifact_root);
          },
          {},
          producer,
          QSet<QString>(
              authorization.asset_ids.cbegin(),
              authorization.asset_ids.cend()));
  if (!batch_result.valid) {
    result.error = batch_result.error;
    return result;
  }
  QFile report_file(report_path);
  if (!report_file.open(QIODevice::ReadOnly)) {
    result.error = QStringLiteral(
                       "migration report could not be read after persistence: %1")
                       .arg(report_file.errorString());
    return result;
  }
  result.report_file_sha256 = QString::fromLatin1(
      QCryptographicHash::hash(
          report_file.readAll(),
          QCryptographicHash::Sha256)
          .toHex());
  if (result.report_file_sha256.size() != 64) {
    result.error =
        QStringLiteral("migration report file hash generation failed");
    return result;
  }
  result.report = batch_result.report;
  result.valid = true;
  return result;
}

RenderingMigrationBatchCommandResult
run_packaged_rendering_migration_report_merge_command(
    const QString& report_path,
    const QStringList& component_report_paths,
    const QStringList& component_receipt_paths) {
  RenderingMigrationBatchCommandResult result;
  if (report_path.trimmed().isEmpty() ||
      component_report_paths.size() < 2) {
    result.error = QStringLiteral(
        "migration report merge requires an output and at least two component reports");
    return result;
  }
  const auto receipt_validation =
      validate_rendering_migration_component_receipts(
          component_report_paths, component_receipt_paths);
  if (!receipt_validation.valid) {
    result.error = receipt_validation.error;
    return result;
  }
  QVariantMap base_report;
  if (!read_report(
          component_report_paths.front(),
          &base_report,
          &result.error)) {
    return result;
  }
  std::vector<QVariantMap> overlays;
  overlays.reserve(
      static_cast<std::size_t>(component_report_paths.size() - 1));
  QMap<QString, QString> artifact_report_by_asset;
  const auto index_artifact_sources =
      [&artifact_report_by_asset](
          const QVariantMap& report,
          const QString& path) {
        for (const auto& value :
             report.value(QStringLiteral("assets")).toList()) {
          const auto asset = value.toMap();
          const auto asset_id =
              asset.value(QStringLiteral("assetId"))
                  .toString()
                  .trimmed();
          if (!asset_id.isEmpty()) {
            artifact_report_by_asset.insert(asset_id, path);
          }
        }
      };
  index_artifact_sources(
      base_report, component_report_paths.front());
  for (qsizetype index = 1;
       index < component_report_paths.size();
       ++index) {
    QVariantMap overlay;
    if (!read_report(
            component_report_paths.at(index),
            &overlay,
            &result.error)) {
      return result;
    }
    index_artifact_sources(
        overlay, component_report_paths.at(index));
    overlays.push_back(std::move(overlay));
  }
  result.report = merge_rendering_migration_batch_reports(
      base_report, overlays);
  if (!result.report.valid) {
    result.error = result.report.error;
    return result;
  }
  result.evidence_artifact_root =
      report_path + QStringLiteral(".assets");
  if (!QDir().mkpath(result.evidence_artifact_root)) {
    result.error = QStringLiteral(
        "merged migration artifact directory could not be created");
    return result;
  }
  const QRegularExpression artifact_identity(
      QStringLiteral("sha256-([0-9a-f]{64})\\."));
  for (const auto& value :
       result.report.document
           .value(QStringLiteral("assets"))
           .toList()) {
    const auto asset = value.toMap();
    const auto asset_id =
        asset.value(QStringLiteral("assetId"))
            .toString();
    const auto component_path =
        artifact_report_by_asset.value(asset_id);
    const auto component_root =
        component_path + QStringLiteral(".assets");
    for (const auto& file_name :
         asset.value(QStringLiteral("evidenceArtifactFileNames"))
             .toStringList()) {
      const auto match = artifact_identity.match(file_name);
      if (!match.hasMatch() || file_name.contains(QLatin1Char('/'))) {
        result.error = QStringLiteral(
                           "migration artifact file identity is invalid for %1")
                           .arg(asset_id);
        return result;
      }
      if (!materialize_artifact(
              QDir(component_root).filePath(file_name),
              QDir(result.evidence_artifact_root)
                  .filePath(file_name),
              match.captured(1),
              &result.error)) {
        return result;
      }
    }
  }
  if (!write_immutable_rendering_migration_batch_report(
          report_path,
          result.report.document,
          &result.error)) {
    return result;
  }
  result.report_file_sha256 = sha256_file(report_path);
  if (result.report_file_sha256.size() != 64) {
    result.error = QStringLiteral(
        "merged migration report file hash generation failed");
    return result;
  }
  result.valid = true;
  return result;
}

}  // namespace photo_editor::desktop
