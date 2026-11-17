#pragma once
#include "desktop_source_geometry.h"
#include <QSet>
#include <QTransform>
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace photo_editor::desktop {
struct VectorMaskPlacement {
  QTransform local_to_canvas;  // Normalized layer frame -> pre-parent execution pixels.
  QTransform local_to_source;  // Normalized layer frame -> original source pixel edges.
  QTransform source_to_canvas; // Original source pixel edges -> pre-parent execution pixels.
  bool valid = false;
};
// Shares the compiler's rounded raster placement and compositor's group pivot.
// Masks are sampled after a layer's own transform, before its parent groups.
inline VectorMaskPlacement vector_mask_placement(const QVariantMap& layer,
    const QVariantList& layers, const DesktopSourceGeometry& geometry, QSize output = {}) {
  if (!geometry.valid()) return {};
  if (output.isEmpty()) output = geometry.crop_rect.size();
  struct Placement { double x, y, width, height, scale, rotation; };
  const auto apply = [](QPointF p, const Placement& t) {
    const auto angle = t.rotation * std::numbers::pi / 180.0;
    const auto dx = (p.x() - t.width * 0.5) * t.scale;
    const auto dy = (p.y() - t.height * 0.5) * t.scale;
    return QPointF(t.x + t.width * t.scale * 0.5 + dx * std::cos(angle) - dy * std::sin(angle),
        t.y + t.height * t.scale * 0.5 + dx * std::sin(angle) + dy * std::cos(angle));
  };
  const auto placement = [&](const QVariantMap& value, bool group) {
    const auto canvas_width = value.value("canvasWidth").toInt(), canvas_height = value.value("canvasHeight").toInt();
    const bool has_canvas = canvas_width > 0 && canvas_height > 0;
    const auto cw = static_cast<double>((group ? canvas_width > 0 : has_canvas) ? canvas_width : output.width());
    const auto ch = static_cast<double>((group ? canvas_height > 0 : has_canvas) ? canvas_height : output.height());
    auto sx = output.width() / cw, sy = output.height() / ch;
    double ox = 0, oy = 0;
    if (!group && has_canvas && value.value("kind").toString() == "pixel" && value.value("coordinateSpace").toString() == "source-area") {
      const auto basis = value.value("coordinateArea").toMap(); const auto current = geometry.edge_area();
      if (basis.value("width").toDouble() <= 0 || basis.value("height").toDouble() <= 0) return Placement{0, 0, 0, 0, 1, 0};
      sx *= basis.value("width").toDouble() / current.width();
      sy *= basis.value("height").toDouble() / current.height();
      ox = (basis.value("x").toDouble() - current.x()) / current.width() * output.width();
      oy = (basis.value("y").toDouble() - current.y()) / current.height() * output.height();
    }
    const bool text = value.value("kind").toString() == "text";
    const auto width = text ? std::clamp(value.value("pixelWidth", 512).toInt(), 16, 8192) : value.value("pixelWidth").toInt();
    const auto height = text ? std::clamp(value.value("pixelHeight", 128).toInt(), 16, 8192) : value.value("pixelHeight").toInt();
    return Placement{std::round(value.value("x").toInt() * sx + ox), std::round(value.value("y").toInt() * sy + oy),
        group ? static_cast<double>(output.width()) : std::max(1.0, std::round(width * sx)),
        group ? static_cast<double>(output.height()) : std::max(1.0, std::round(height * sy)),
        std::clamp(value.value("scale", 1.0).toDouble(), 0.05, 32.0),
        std::clamp(value.value("rotation").toDouble(), -3600.0, 3600.0)};
  };
  const auto kind = layer.value("kind").toString();
  const bool canvas_kind = kind == "fill" || kind == "adjustment";
  auto own = canvas_kind ? Placement{0, 0, static_cast<double>(output.width()), static_cast<double>(output.height()), 1, 0}
      : placement(layer, kind == "group" || layer.value("copyBaseImage").toBool());
  if (own.width <= 0 || own.height <= 0) return {};
  QList<Placement> parents; QSet<QString> seen; seen.insert(layer.value("id").toString());
  const auto scope_root = layer.value("_smartDocumentRootId").toString();
  int fixed_parent_count = -1;
  auto parent = layer.value("parentId").toString();
  while (!parent.isEmpty()) {
    if (seen.contains(parent) || seen.size() > 64) return {};
    if (parent == scope_root) fixed_parent_count = static_cast<int>(parents.size());
    seen.insert(parent); QVariantMap value;
    for (const auto& item : layers) if (item.toMap().value("id").toString() == parent) { value = item.toMap(); break; }
    if (value.value("kind").toString() != "group") return {};
    const auto p = placement(value, true); if (p.width <= 0 || p.height <= 0) return {};
    parents.append(p); parent = value.value("parentId").toString();
  }
  const auto affine = [](auto map) {
    const auto o = map(QPointF(0, 0)); const auto x = map(QPointF(1, 0)) - o; const auto y = map(QPointF(0, 1)) - o;
    return QTransform(x.x(), x.y(), y.x(), y.y(), o.x(), o.y());
  };
  const auto parent_map = [&](QPointF p) { for (const auto& t : parents) p = apply(p, t); return p; };
  const auto local = affine([&](QPointF p) { return apply({p.x() * own.width, p.y() * own.height}, own); });
  const auto source = affine([&](QPointF p) {
    p = parent_map(local.map(p));
    return QPointF(geometry.crop_rect.x() + p.x() * geometry.crop_rect.width() / output.width(),
        geometry.crop_rect.y() + p.y() * geometry.crop_rect.height() / output.height());
  });
  const auto fixed_parent_map = [&](QPointF p) {
    const auto count = fixed_parent_count < 0 ? static_cast<int>(parents.size()) : fixed_parent_count;
    for (int i = 0; i < count; ++i) p = apply(p, parents[i]);
    return p;
  };
  const auto inverse_parents = affine(fixed_parent_map).inverted();
  const auto source_to_canvas = affine([&](QPointF p) {
    return inverse_parents.map(QPointF((p.x() - geometry.crop_rect.x()) * output.width() / geometry.crop_rect.width(),
        (p.y() - geometry.crop_rect.y()) * output.height() / geometry.crop_rect.height()));
  });
  const auto finite = [](const QTransform& t) { return t.isInvertible() && std::isfinite(t.m11()) && std::isfinite(t.m12()) &&
      std::isfinite(t.m21()) && std::isfinite(t.m22()) && std::isfinite(t.dx()) && std::isfinite(t.dy()); };
  if (!finite(local) || !finite(source) || !finite(source_to_canvas)) return {};
  return {local, source, source_to_canvas, true};
}
}
