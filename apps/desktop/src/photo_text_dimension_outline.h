#pragma once

#include <QColor>
#include <QPainterPath>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace photo_editor::desktop {

struct PhotoTextDimensionOutline final {
  QPainterPath path;
  QColor color;
};

struct PhotoTextDimensionOutlines final {
  QVector<PhotoTextDimensionOutline> faces;
  QString error;
};

// Shape through the same fonts, character runs and paragraph metrics as the
// ordinary text renderer. Paths stay in the authored text box's document pixels.
[[nodiscard]] PhotoTextDimensionOutlines photo_text_dimension_outlines(
    const QVariantMap& layer);

}  // namespace photo_editor::desktop
