#pragma once

#include <QString>

namespace photo_editor::desktop {
// Sparkle always includes the untagged channel in allowedChannelsForUpdater.
// Staging must explicitly reject it so a misplaced production item cannot
// replace a private test build and switch its account service.
inline bool updateChannelAllowed(const QString& build_channel, const QString& item_channel) {
  if (build_channel == QStringLiteral("staging"))
    return item_channel == QStringLiteral("staging");
  if (build_channel == QStringLiteral("beta"))
    return item_channel.isEmpty() || item_channel == QStringLiteral("beta");
  return build_channel == QStringLiteral("stable") && item_channel.isEmpty();
}
}  // namespace photo_editor::desktop
