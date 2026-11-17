#include "scope_plot_item.h"

#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QSGRendererInterface>
#include <QSGSimpleTextureNode>
#include <QQuickWindow>
#include <QPainter>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace photo_editor::desktop {
namespace {
using Vertex = QSGGeometry::ColoredPoint2D;
void rectangle(std::vector<Vertex>& vertices, float x, float y, float w, float h,
               const QColor& color, double opacity) {
  const auto alpha = std::clamp(opacity * color.alphaF(), 0.0, 1.0);
  const auto byte = [](double value) { return static_cast<uchar>(std::clamp(value * 255.0, 0.0, 255.0)); };
  const auto r = byte(color.redF() * alpha), g = byte(color.greenF() * alpha);
  const auto b = byte(color.blueF() * alpha), a = byte(alpha);
  for (const auto point : {QPointF(x, y), QPointF(x + w, y), QPointF(x, y + h),
                           QPointF(x + w, y), QPointF(x + w, y + h), QPointF(x, y + h)}) {
    Vertex vertex;
    vertex.set(static_cast<float>(point.x()), static_cast<float>(point.y()), r, g, b, a);
    vertices.push_back(vertex);
  }
}
void append_node(QSGNode* root, const std::vector<Vertex>& vertices) {
  if (vertices.empty()) return;
  auto* node = new QSGGeometryNode;
  auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(),
                                  static_cast<int>(vertices.size()));
  geometry->setDrawingMode(QSGGeometry::DrawTriangles);
  std::memcpy(geometry->vertexData(), vertices.data(), vertices.size() * sizeof(Vertex));
  node->setGeometry(geometry);
  node->setFlag(QSGNode::OwnsGeometry);
  node->setMaterial(new QSGVertexColorMaterial);
  node->setFlag(QSGNode::OwnsMaterial);
  root->appendChildNode(node);
}
}

ScopePlotItem::ScopePlotItem(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents);
}
void ScopePlotItem::setFrame(const QVariantMap& value) {
  frame_ = value; update(); emit frameChanged();
}
void ScopePlotItem::setReference(const QVariantMap& value) {
  reference_ = value; update(); emit referenceChanged();
}
void ScopePlotItem::setOptions(const QVariantMap& value) {
  options_ = value; update(); emit optionsChanged();
}

QSGNode* ScopePlotItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
  delete old;
  auto* root = new QSGNode;
  const auto w = static_cast<float>(width()), h = static_cast<float>(height());
  if (w <= 0 || h <= 0) return root;
  const auto mode = options_.value(QStringLiteral("mode")).toString();
  const auto gain_value = options_.value(QStringLiteral("gain"), 1.0).toDouble();
  const auto gain = std::isfinite(gain_value) ? std::clamp(gain_value, 0.1, 8.0) : 1.0;
  const bool rgb = options_.value(QStringLiteral("rgb"), true).toBool();
  const bool luma = options_.value(QStringLiteral("luma"), true).toBool();
  const auto palette = options_.value(QStringLiteral("palette")).toList();
  const std::array<QColor, 4> defaults{QColor(255, 100, 110), QColor(100, 235, 145),
                                     QColor(100, 170, 255), QColor(240, 240, 240)};
  const auto draw = [&](const QVariantMap& frame, bool reference) {
    const auto columns = std::clamp(frame.value(QStringLiteral("columns"), 128).toInt(), 16, 512);
    const auto levels = std::clamp(frame.value(QStringLiteral("levels"), 64).toInt(), 16, 256);
    std::vector<Vertex> vertices;
    if (mode == QStringLiteral("Vectorscope")) {
      const auto points = frame.value(QStringLiteral("vectors")).toList();
      const auto size = std::min(w, h);
      const auto left = (w - size) / 2, top = (h - size) / 2;
      for (const auto& value : points.mid(0, 16384)) {
        const auto point = value.toMap();
        const auto x = point.value(QStringLiteral("x")).toDouble();
        const auto y = point.value(QStringLiteral("y")).toDouble();
        if (!std::isfinite(x) || !std::isfinite(y)) continue;
        const auto color = reference ? QColor(240, 240, 240) : QColor::fromRgbF(
            static_cast<float>(std::clamp(point.value(QStringLiteral("red")).toDouble(), 0.0, 1.0)),
            static_cast<float>(std::clamp(point.value(QStringLiteral("green")).toDouble(), 0.0, 1.0)),
            static_cast<float>(std::clamp(point.value(QStringLiteral("blue")).toDouble(), 0.0, 1.0)));
        rectangle(vertices, left + static_cast<float>(x) * size, top + static_cast<float>(y) * size,
                  1.5f, 1.5f, color, reference ? 0.18 : std::min(1.0, 0.55 * gain));
      }
      append_node(root, vertices);
      return;
    }
    const std::array<QString, 4> names{QStringLiteral("red"), QStringLiteral("green"),
                                      QStringLiteral("blue"), QStringLiteral("luma")};
    const std::array<QString, 4> traces{QStringLiteral("traceRed"), QStringLiteral("traceGreen"),
                                       QStringLiteral("traceBlue"), QStringLiteral("traceLuma")};
    const bool parade = mode == QStringLiteral("Parade");
    for (int channel = 0; channel < 4; ++channel) {
      if ((channel < 3 && !rgb) || (channel == 3 && (!luma || parade))) continue;
      vertices.clear();
      const auto data = frame.value(names[static_cast<std::size_t>(channel)]).toList();
      auto color = palette.size() > channel ? palette[channel].value<QColor>() : defaults[static_cast<std::size_t>(channel)];
      if (reference) color = QColor(240, 240, 240);
      const float plot_width = parade ? (w - 16) / 3 : w;
      const float left = parade ? static_cast<float>(channel) * (plot_width + 8) : 0;
      const auto dx = plot_width / static_cast<float>(columns), dy = h / static_cast<float>(levels);
      if (options_.value(QStringLiteral("density"), true).toBool() && data.size() == columns * levels) {
        vertices.reserve(static_cast<std::size_t>(columns * levels));
        for (int x = 0; x < columns; ++x) {
          for (int y = 0; y < levels; ++y) {
            const auto density = data[x * levels + y].toDouble();
            if (!std::isfinite(density) || density <= 0.008) continue;
            const auto alpha = std::min(0.80, std::pow(density, 1.35) * gain * (reference ? 0.18 : 0.72));
            rectangle(vertices, left + static_cast<float>(x) * dx, h - static_cast<float>(y + 1) * dy,
                      std::max(1.0f, dx), std::max(1.0f, dy), color, alpha);
          }
        }
      }
      if (options_.value(QStringLiteral("trace"), false).toBool()) {
        const auto trace = frame.value(traces[static_cast<std::size_t>(channel)]).toList();
        for (qsizetype i = 0; i < trace.size(); ++i) {
          const auto value = trace[i].toMap().value(QStringLiteral("avg")).toDouble();
          if (!std::isfinite(value)) continue;
          rectangle(vertices, left + static_cast<float>(i) * plot_width / static_cast<float>(std::max<qsizetype>(1, trace.size() - 1)),
                    h * static_cast<float>(1.0 - std::clamp(value, 0.0, 1.0)), 2, 2,
                    color, reference ? 0.18 : 0.65);
        }
      }
      append_node(root, vertices);
    }
  };
  draw(reference_, true);
  draw(frame_, false);
  // Qt's software scene graph does not draw custom vertex-color materials.
  // Retain a bounded native raster fallback for headless/unsupported-GPU use.
  if (window() && window()->rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
    const auto ratio = window()->devicePixelRatio();
    QImage image(QSize(std::max(1, qCeil(width() * ratio)), std::max(1, qCeil(height() * ratio))),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    for (auto* child = root->firstChild(); child; child = child->nextSibling()) {
      auto* geometry = static_cast<QSGGeometryNode*>(child)->geometry();
      const auto* vertices = geometry->vertexDataAsColoredPoint2D();
      for (int index = 0; index + 5 < geometry->vertexCount(); index += 6) {
        const auto& first = vertices[index];
        const auto& last = vertices[index + 4];
        painter.fillRect(QRectF(first.x, first.y, last.x - first.x, last.y - first.y),
                         QColor::fromRgba(qUnpremultiply(qRgba(first.r, first.g, first.b, first.a))));
      }
    }
    painter.end();
    delete root;
    auto* texture = new QSGSimpleTextureNode;
    texture->setTexture(window()->createTextureFromImage(image));
    texture->setOwnsTexture(true);
    texture->setRect(boundingRect());
    return texture;
  }
  return root;
}
}  // namespace photo_editor::desktop
