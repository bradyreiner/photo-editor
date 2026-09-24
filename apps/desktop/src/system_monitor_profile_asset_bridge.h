#pragma once

#include "system_monitor_profile_state.h"

#include "photo_editor/pipeline/color_profile_asset.h"

#include <QCryptographicHash>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace photo_editor::desktop {

inline photo_editor::pipeline::ResolvedIccProfileAsset
resolve_system_monitor_profile_asset(
    const SystemMonitorProfileSnapshot& snapshot,
    qsizetype maximum_profile_bytes) {
  if (!snapshot.valid()) {
    return {};
  }
  struct Cache final {
    std::mutex mutex;
    QString identity;
    photo_editor::pipeline::ResolvedIccProfileAsset asset;
  };
  static Cache cache;
  {
    const std::scoped_lock lock(cache.mutex);
    if (cache.identity == snapshot.identity && cache.asset.ready()) {
      return cache.asset;
    }
  }

  const auto payload = snapshot.color_space.iccProfile();
  if (payload.isEmpty() || payload.size() > maximum_profile_bytes) {
    return {};
  }
  const auto digest =
      QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
  const auto digest_string = QString::fromLatin1(digest);
  std::vector<std::byte> bytes(static_cast<std::size_t>(payload.size()));
  std::memcpy(
      bytes.data(),
      payload.constData(),
      static_cast<std::size_t>(payload.size()));
  auto resolved =
      photo_editor::pipeline::default_icc_profile_asset_registry()
          .register_payload(
              photo_editor::pipeline::IccProfileAssetReference{
                  .asset_id =
                      QStringLiteral("system-monitor:%1")
                          .arg(digest_string)
                          .toStdString(),
                  .payload_sha256 = digest_string.toStdString(),
                  .display_name = snapshot.display_name.toStdString(),
                  .source_uri = snapshot.identity.toStdString(),
                  .source_kind = "system-monitor",
                  .byte_size =
                      static_cast<std::uint64_t>(bytes.size()),
              },
              bytes,
              snapshot.identity.toStdString());
  if (resolved.ready()) {
    const std::scoped_lock lock(cache.mutex);
    cache.identity = snapshot.identity;
    cache.asset = resolved;
  }
  return resolved;
}

}  // namespace photo_editor::desktop
