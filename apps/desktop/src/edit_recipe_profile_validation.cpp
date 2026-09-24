#include "edit_recipe_profile_validation.h"

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {

namespace {

bool valid_sha256(const QString& candidate) {
  if (candidate.size() != 64) {
    return false;
  }
  return std::all_of(
      candidate.cbegin(),
      candidate.cend(),
      [](QChar character) {
        const auto lower = character.toLower();
        return character.isDigit() ||
               (lower >= QLatin1Char('a') && lower <= QLatin1Char('f'));
      });
}

}  // namespace

void validate_edit_recipe_profile_contract(
    const QJsonObject& color_pipeline,
    QStringList* errors) {
  if (errors == nullptr) {
    return;
  }

  if (!color_pipeline.value(QStringLiteral("cameraProfileAsset")).isObject()) {
    errors->append(
        QStringLiteral("colorPipeline.cameraProfileAsset must be an object"));
  } else {
    const auto camera_profile_asset =
        color_pipeline.value(QStringLiteral("cameraProfileAsset")).toObject();
    if (!camera_profile_asset.isEmpty()) {
      for (const auto& prohibited : {
               QStringLiteral("payload"),
               QStringLiteral("bytes"),
               QStringLiteral("base64")}) {
        if (camera_profile_asset.contains(prohibited)) {
          errors->append(
              QStringLiteral(
                  "colorPipeline.cameraProfileAsset.%1 is prohibited; recipes store identity, not DCP bytes")
                  .arg(prohibited));
        }
      }
      if (!camera_profile_asset.value(QStringLiteral("schemaVersion")).isDouble() ||
          camera_profile_asset.value(QStringLiteral("schemaVersion")).toInt() !=
              1) {
        errors->append(
            QStringLiteral(
                "colorPipeline.cameraProfileAsset.schemaVersion must be 1"));
      }
      for (const auto& key : {
               QStringLiteral("assetId"),
               QStringLiteral("payloadSha256"),
               QStringLiteral("displayName"),
               QStringLiteral("sourceUri"),
               QStringLiteral("sourceKind")}) {
        if (!camera_profile_asset.value(key).isString()) {
          errors->append(
              QStringLiteral(
                  "colorPipeline.cameraProfileAsset.%1 must be a string")
                  .arg(key));
        }
      }
      if (!valid_sha256(
              camera_profile_asset.value(QStringLiteral("payloadSha256"))
                  .toString())) {
        errors->append(
            QStringLiteral(
                "colorPipeline.cameraProfileAsset.payloadSha256 must be 64 hexadecimal characters"));
      }
      constexpr auto kMaximumPersistedDcpProfileBytes =
          64.0 * 1024.0 * 1024.0;
      const auto byte_size =
          camera_profile_asset.value(QStringLiteral("byteSize"));
      if (!byte_size.isDouble() || byte_size.toDouble() <= 0.0 ||
          byte_size.toDouble() > kMaximumPersistedDcpProfileBytes) {
        errors->append(
            QStringLiteral(
                "colorPipeline.cameraProfileAsset.byteSize must be between 1 and 67108864"));
      }
    }
  }

  if (!color_pipeline
           .value(QStringLiteral("cameraProfileRenderingModel"))
           .isString()) {
    errors->append(
        QStringLiteral(
            "colorPipeline.cameraProfileRenderingModel must be a string"));
  } else {
    const auto rendering_model =
        color_pipeline
            .value(QStringLiteral("cameraProfileRenderingModel"))
            .toString();
    if (rendering_model != QStringLiteral("matrix-only-v1") &&
        rendering_model != QStringLiteral("dng-profile-v2") &&
        rendering_model != QStringLiteral("adobe-dng-profile-v2")) {
      errors->append(
          QStringLiteral(
              "colorPipeline.cameraProfileRenderingModel must be matrix-only-v1 or dng-profile-v2"));
    }
  }

  if (!color_pipeline.value(QStringLiteral("profileAssets")).isObject()) {
    errors->append(
        QStringLiteral("colorPipeline.profileAssets must be an object"));
    return;
  }

  const auto profile_assets =
      color_pipeline.value(QStringLiteral("profileAssets")).toObject();
  constexpr auto kMaximumPersistedIccProfileBytes =
      16.0 * 1024.0 * 1024.0;
  for (const auto& slot : {
           QStringLiteral("input"),
           QStringLiteral("preview"),
           QStringLiteral("proof"),
           QStringLiteral("export")}) {
    if (!profile_assets.value(slot).isObject()) {
      errors->append(
          QStringLiteral("colorPipeline.profileAssets.%1 must be an object")
              .arg(slot));
      continue;
    }
    const auto profile = profile_assets.value(slot).toObject();
    if (profile.isEmpty()) {
      continue;
    }
    for (const auto& prohibited : {
             QStringLiteral("payload"),
             QStringLiteral("bytes"),
             QStringLiteral("base64")}) {
      if (profile.contains(prohibited)) {
        errors->append(
            QStringLiteral(
                "colorPipeline.profileAssets.%1.%2 is prohibited; recipes store identity, not ICC bytes")
                .arg(slot, prohibited));
      }
    }
    for (const auto& key : {
             QStringLiteral("assetId"),
             QStringLiteral("payloadSha256"),
             QStringLiteral("displayName"),
             QStringLiteral("sourceUri"),
             QStringLiteral("sourceKind")}) {
      if (!profile.value(key).isString()) {
        errors->append(
            QStringLiteral(
                "colorPipeline.profileAssets.%1.%2 must be a string")
                .arg(slot, key));
      }
    }
    if (profile.value(QStringLiteral("assetId"))
            .toString()
            .trimmed()
            .isEmpty()) {
      errors->append(
          QStringLiteral(
              "colorPipeline.profileAssets.%1.assetId must not be empty")
              .arg(slot));
    }
    if (!valid_sha256(
            profile.value(QStringLiteral("payloadSha256"))
                .toString()
                .trimmed())) {
      errors->append(
          QStringLiteral(
              "colorPipeline.profileAssets.%1.payloadSha256 must be 64 hexadecimal characters")
              .arg(slot));
    }
    const auto byte_size = profile.value(QStringLiteral("byteSize"));
    if (!byte_size.isDouble() || byte_size.toDouble() < 1.0 ||
        byte_size.toDouble() > kMaximumPersistedIccProfileBytes ||
        std::floor(byte_size.toDouble()) != byte_size.toDouble()) {
      errors->append(
          QStringLiteral(
              "colorPipeline.profileAssets.%1.byteSize must be an integer from 1 through 16777216")
              .arg(slot));
    }
  }
}

}  // namespace photo_editor::desktop
