#include "rendering_migration_chunk_receipt.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QRegularExpression>
#include <QSet>
#include <QVariantMap>

namespace photo_editor::desktop {
namespace {

constexpr auto kReceiptContract =
    "photo-editor-rendering-migration-refresh-chunk-receipt:v1";
constexpr auto kAuthorizationContract =
    "photo-editor-rendering-migration-refresh-chunk-authorization:v1";

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

[[nodiscard]] bool read_object(
    const QString& path,
    QVariantMap* object,
    QString* error) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    *error = QStringLiteral("migration receipt input could not be read: %1")
                 .arg(path);
    return false;
  }
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    *error = QStringLiteral("migration receipt input is invalid JSON: %1")
                 .arg(path);
    return false;
  }
  *object = document.object().toVariantMap();
  return true;
}

[[nodiscard]] bool valid_sha256(const QString& value) {
  static const QRegularExpression pattern(
      QStringLiteral("^[0-9a-f]{64}$"));
  return pattern.match(value).hasMatch();
}

[[nodiscard]] bool integer_variant(const QVariant& value) {
  const auto type = value.metaType().id();
  return type == QMetaType::Int || type == QMetaType::UInt ||
         type == QMetaType::LongLong || type == QMetaType::ULongLong;
}

[[nodiscard]] QSet<QString> keys_of(const QVariantMap& value) {
  QSet<QString> keys;
  for (auto iterator = value.cbegin(); iterator != value.cend(); ++iterator) {
    keys.insert(iterator.key());
  }
  return keys;
}

[[nodiscard]] bool exact_asset_ids(
    const QVariant& value,
    QSet<QString>* ids) {
  const auto values = value.toList();
  if (values.isEmpty()) {
    return false;
  }
  for (const auto& item : values) {
    if (item.metaType().id() != QMetaType::QString) {
      return false;
    }
    const auto id = item.toString().trimmed();
    if (id.isEmpty() || ids->contains(id)) {
      return false;
    }
    ids->insert(id);
  }
  return true;
}

[[nodiscard]] bool report_asset_ids(
    const QVariantMap& report,
    QSet<QString>* ids) {
  const auto assets = report.value(QStringLiteral("assets")).toList();
  if (assets.isEmpty()) {
    return false;
  }
  for (const auto& value : assets) {
    const auto asset = value.toMap();
    const auto id = asset.value(QStringLiteral("assetId")).toString().trimmed();
    if (id.isEmpty() || ids->contains(id)) {
      return false;
    }
    ids->insert(id);
  }
  return true;
}

}  // namespace

RenderingMigrationChunkAuthorizationResult
validate_rendering_migration_chunk_authorization(
    const QString& authorization_path,
    const QString& executable_sha256) {
  RenderingMigrationChunkAuthorizationResult result;
  QVariantMap authorization;
  if (!read_object(authorization_path, &authorization, &result.error)) {
    return result;
  }
  const QSet<QString> expected_keys{
      QStringLiteral("contract"),
      QStringLiteral("status"),
      QStringLiteral("producer"),
      QStringLiteral("planSha256"),
      QStringLiteral("planFileSha256"),
      QStringLiteral("chunkIndex"),
      QStringLiteral("assetIds"),
      QStringLiteral("assetCount"),
      QStringLiteral("totalPixels"),
      QStringLiteral("overBudgetSingleAsset"),
      QStringLiteral("authorizationSha256"),
  };
  if (keys_of(authorization) != expected_keys ||
      authorization.value(QStringLiteral("contract")).toString() !=
          QString::fromLatin1(kAuthorizationContract) ||
      authorization.value(QStringLiteral("status")).toString() !=
          QStringLiteral("authorized")) {
    result.error = QStringLiteral("migration chunk authorization schema is invalid");
    return result;
  }
  const auto claimed =
      authorization.value(QStringLiteral("authorizationSha256")).toString();
  auto unsigned_authorization = authorization;
  unsigned_authorization.remove(QStringLiteral("authorizationSha256"));
  const auto actual = QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument::fromVariant(unsigned_authorization)
              .toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha256)
          .toHex());
  if (!valid_sha256(claimed) || actual != claimed) {
    result.error = QStringLiteral("migration chunk authorization identity is invalid");
    return result;
  }
  result.plan_sha256 =
      authorization.value(QStringLiteral("planSha256")).toString();
  result.plan_file_sha256 =
      authorization.value(QStringLiteral("planFileSha256")).toString();
  QSet<QString> asset_ids;
  const auto producer = authorization.value(QStringLiteral("producer")).toMap();
  const QSet<QString> expected_producer_keys{
      QStringLiteral("contract"),
      QStringLiteral("executableSha256"),
      QStringLiteral("executableName"),
      QStringLiteral("buildMode"),
  };
  if (!valid_sha256(result.plan_sha256) ||
      !valid_sha256(result.plan_file_sha256) ||
      !integer_variant(authorization.value(QStringLiteral("chunkIndex"))) ||
      authorization.value(QStringLiteral("chunkIndex")).toLongLong() <= 0 ||
      !exact_asset_ids(
          authorization.value(QStringLiteral("assetIds")), &asset_ids) ||
      !integer_variant(authorization.value(QStringLiteral("assetCount"))) ||
      authorization.value(QStringLiteral("assetCount")).toLongLong() !=
          static_cast<qlonglong>(asset_ids.size()) ||
      !integer_variant(authorization.value(QStringLiteral("totalPixels"))) ||
      authorization.value(QStringLiteral("totalPixels")).toLongLong() <= 0 ||
      authorization.value(QStringLiteral("overBudgetSingleAsset"))
              .metaType()
              .id() != QMetaType::Bool ||
      keys_of(producer) != expected_producer_keys ||
      producer.value(QStringLiteral("contract")).toString() !=
          QStringLiteral(
              "photo-editor-rendering-migration-batch-producer:v1") ||
      producer.value(QStringLiteral("executableName")).toString().isEmpty() ||
      producer.value(QStringLiteral("buildMode")).toString().isEmpty() ||
      producer.value(QStringLiteral("executableSha256")).toString() !=
          executable_sha256) {
    result.error = QStringLiteral("migration chunk authorization content is invalid");
    return result;
  }
  result.asset_ids = asset_ids.values();
  result.asset_ids.sort();
  result.producer = producer;
  result.valid = true;
  return result;
}

RenderingMigrationReceiptValidationResult
validate_rendering_migration_component_receipts(
    const QStringList& component_report_paths,
    const QStringList& component_receipt_paths) {
  RenderingMigrationReceiptValidationResult result;
  if (component_report_paths.size() < 2 ||
      component_receipt_paths.size() != component_report_paths.size() - 1) {
    result.error = QStringLiteral(
        "migration report merge requires one receipt for every overlay component");
    return result;
  }
  const QSet<QString> expected_receipt_keys{
      QStringLiteral("contract"),
      QStringLiteral("status"),
      QStringLiteral("producer"),
      QStringLiteral("planSha256"),
      QStringLiteral("planFileSha256"),
      QStringLiteral("chunkIndex"),
      QStringLiteral("assetIds"),
      QStringLiteral("componentReportSha256"),
      QStringLiteral("componentReportFileSha256"),
      QStringLiteral("artifactCount"),
      QStringLiteral("receiptSha256"),
  };
  QSet<qlonglong> chunk_indices;
  QSet<QString> authorized_asset_ids;
  for (qsizetype index = 0; index < component_receipt_paths.size(); ++index) {
    const auto& report_path = component_report_paths.at(index + 1);
    const auto& receipt_path = component_receipt_paths.at(index);
    QVariantMap report;
    QVariantMap receipt;
    if (!read_object(report_path, &report, &result.error) ||
        !read_object(receipt_path, &receipt, &result.error)) {
      return result;
    }
    if (keys_of(receipt) != expected_receipt_keys ||
        receipt.value(QStringLiteral("contract")).toString() !=
            QString::fromLatin1(kReceiptContract) ||
        receipt.value(QStringLiteral("status")).toString() !=
            QStringLiteral("validated-evidence")) {
      result.error = QStringLiteral("migration component receipt schema is invalid");
      return result;
    }
    const auto claimed_receipt_sha =
        receipt.value(QStringLiteral("receiptSha256")).toString();
    auto unsigned_receipt = receipt;
    unsigned_receipt.remove(QStringLiteral("receiptSha256"));
    const auto actual_receipt_sha = QString::fromLatin1(
        QCryptographicHash::hash(
            QJsonDocument::fromVariant(unsigned_receipt)
                .toJson(QJsonDocument::Compact),
            QCryptographicHash::Sha256)
            .toHex());
    if (!valid_sha256(claimed_receipt_sha) ||
        actual_receipt_sha != claimed_receipt_sha) {
      result.error = QStringLiteral("migration component receipt identity is invalid");
      return result;
    }
    const auto plan_sha = receipt.value(QStringLiteral("planSha256")).toString();
    const auto plan_file_sha =
        receipt.value(QStringLiteral("planFileSha256")).toString();
    if (!valid_sha256(plan_sha) || !valid_sha256(plan_file_sha) ||
        (!result.plan_sha256.isEmpty() && result.plan_sha256 != plan_sha) ||
        (!result.plan_file_sha256.isEmpty() &&
         result.plan_file_sha256 != plan_file_sha)) {
      result.error = QStringLiteral(
          "migration component receipts do not belong to one refresh plan");
      return result;
    }
    result.plan_sha256 = plan_sha;
    result.plan_file_sha256 = plan_file_sha;
    const auto chunk_index = receipt.value(QStringLiteral("chunkIndex")).toLongLong();
    if (chunk_index <= 0 || chunk_indices.contains(chunk_index)) {
      result.error = QStringLiteral(
          "migration component receipts contain an invalid or duplicate chunk index");
      return result;
    }
    chunk_indices.insert(chunk_index);
    if (receipt.value(QStringLiteral("artifactCount")).toLongLong() <= 0) {
      result.error = QStringLiteral("migration component receipt has no artifacts");
      return result;
    }
    const auto report_file_sha = sha256_file(report_path);
    if (receipt.value(QStringLiteral("componentReportFileSha256")).toString() !=
            report_file_sha ||
        receipt.value(QStringLiteral("componentReportSha256")).toString() !=
            report.value(QStringLiteral("reportSha256")).toString() ||
        receipt.value(QStringLiteral("producer")).toMap() !=
            report.value(QStringLiteral("producer")).toMap()) {
      result.error = QStringLiteral(
          "migration component receipt does not match its report");
      return result;
    }
    QSet<QString> receipt_ids;
    QSet<QString> report_ids;
    if (!exact_asset_ids(receipt.value(QStringLiteral("assetIds")), &receipt_ids) ||
        !report_asset_ids(report, &report_ids) || receipt_ids != report_ids) {
      result.error = QStringLiteral(
          "migration component receipt asset set does not match its report");
      return result;
    }
    for (const auto& asset_id : receipt_ids) {
      if (authorized_asset_ids.contains(asset_id)) {
        result.error = QStringLiteral(
            "migration component receipts authorize a duplicate asset");
        return result;
      }
      authorized_asset_ids.insert(asset_id);
    }
  }
  result.valid = true;
  return result;
}

}  // namespace photo_editor::desktop
