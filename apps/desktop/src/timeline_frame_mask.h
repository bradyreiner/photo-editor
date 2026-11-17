#pragma once

#include <QString>

namespace photo_editor::desktop::timeline_motion {

// Apply an antialiased normalized-radius rounded rectangle to the current
// RGBA frame. The radius expression can be animated by FFmpeg's frame clock.
[[nodiscard]] inline QString rounded_rect_alpha_filter(
    const QString& normalized_radius_expression) {
  return QStringLiteral(
      "format=rgba,geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)':"
      "a='alpha(X,Y)*clip(0.5-(sqrt(pow(max(abs(X+0.5-W/2)-W/2+(%1)*min(W,H),0),2)+"
      "pow(max(abs(Y+0.5-H/2)-H/2+(%1)*min(W,H),0),2))+"
      "min(max(abs(X+0.5-W/2)-W/2+(%1)*min(W,H),"
      "abs(Y+0.5-H/2)-H/2+(%1)*min(W,H)),0)-(%1)*min(W,H)),0,1)',")
      .arg(normalized_radius_expression);
}

}  // namespace photo_editor::desktop::timeline_motion
