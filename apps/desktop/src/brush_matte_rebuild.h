#pragma once

#include <QSize>
#include <QString>
#include <QVariantMap>

namespace photo_editor::desktop {

[[nodiscard]] QVariantMap mask_with_rebuilt_brush_matte(
    const QVariantMap& mask, const QSize& target_size, const QString& purpose);

}  // namespace photo_editor::desktop
