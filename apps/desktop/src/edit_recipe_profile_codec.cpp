#include "edit_recipe_state.h"

namespace photo_editor::desktop {

IccProfileAssetState parse_icc_profile_asset_state(const QJsonValue& value) {
  if (!value.isObject()) {
    return {};
  }
  const auto object = value.toObject();
  return IccProfileAssetState{
      .asset_id =
          object.value(QStringLiteral("assetId")).toString().trimmed(),
      .payload_sha256 =
          object.value(QStringLiteral("payloadSha256"))
              .toString()
              .trimmed()
              .toLower(),
      .display_name =
          object.value(QStringLiteral("displayName")).toString().trimmed(),
      .source_uri =
          object.value(QStringLiteral("sourceUri")).toString().trimmed(),
      .source_kind =
          object.value(QStringLiteral("sourceKind")).toString().trimmed(),
      .byte_size = qMax<qint64>(
          0,
          static_cast<qint64>(
              object.value(QStringLiteral("byteSize")).toDouble(0.0))),
  };
}

QJsonObject serialize_icc_profile_asset_state(
    const IccProfileAssetState& state) {
  if (!state.configured()) {
    return {};
  }
  QJsonObject object;
  object.insert(QStringLiteral("assetId"), state.asset_id.trimmed());
  object.insert(
      QStringLiteral("payloadSha256"),
      state.payload_sha256.trimmed().toLower());
  object.insert(QStringLiteral("displayName"), state.display_name.trimmed());
  object.insert(QStringLiteral("sourceUri"), state.source_uri.trimmed());
  object.insert(QStringLiteral("sourceKind"), state.source_kind.trimmed());
  object.insert(
      QStringLiteral("byteSize"),
      static_cast<double>(qMax<qint64>(0, state.byte_size)));
  return object;
}

DcpProfileAssetState parse_dcp_profile_asset_state(const QJsonValue& value) {
  if (!value.isObject()) {
    return {};
  }
  const auto object = value.toObject();
  return DcpProfileAssetState{
      .schema_version =
          object.value(QStringLiteral("schemaVersion")).toInt(1),
      .asset_id =
          object.value(QStringLiteral("assetId")).toString().trimmed(),
      .payload_sha256 =
          object.value(QStringLiteral("payloadSha256"))
              .toString()
              .trimmed()
              .toLower(),
      .display_name =
          object.value(QStringLiteral("displayName")).toString().trimmed(),
      .source_uri =
          object.value(QStringLiteral("sourceUri")).toString().trimmed(),
      .source_kind =
          object.value(QStringLiteral("sourceKind")).toString().trimmed(),
      .byte_size = qMax<qint64>(
          0,
          static_cast<qint64>(
              object.value(QStringLiteral("byteSize")).toDouble(0.0))),
  };
}

QJsonObject serialize_dcp_profile_asset_state(
    const DcpProfileAssetState& state) {
  if (!state.configured()) {
    return {};
  }
  QJsonObject object;
  object.insert(QStringLiteral("schemaVersion"), state.schema_version);
  object.insert(QStringLiteral("assetId"), state.asset_id.trimmed());
  object.insert(
      QStringLiteral("payloadSha256"),
      state.payload_sha256.trimmed().toLower());
  object.insert(QStringLiteral("displayName"), state.display_name.trimmed());
  object.insert(QStringLiteral("sourceUri"), state.source_uri.trimmed());
  object.insert(QStringLiteral("sourceKind"), state.source_kind.trimmed());
  object.insert(
      QStringLiteral("byteSize"),
      static_cast<double>(qMax<qint64>(0, state.byte_size)));
  return object;
}

}  // namespace photo_editor::desktop
