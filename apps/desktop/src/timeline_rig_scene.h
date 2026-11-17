#pragma once

// Resolution-independent character animation. The same prepared scene is used
// by vector-generator preview, cached export, and the standalone film renderer.
#include <QBrush>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QSet>
#include <QTransform>
#include <QVariantMap>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <memory>

namespace photo_editor::desktop::timeline_motion {

struct RigKey { double seconds = 0.0, value = 0.0; QString easing; };
struct RigChannel {
  double value = 0.0;
  QVector<RigKey> keys;
  double at(double seconds) const {
    if (keys.isEmpty()) return value;
    const auto next = std::upper_bound(keys.cbegin(), keys.cend(), seconds,
        [](double t, const RigKey& key) { return t < key.seconds; });
    if (next == keys.cbegin()) return next->value;
    if (next == keys.cend()) return keys.constLast().value;
    const auto& a = *(next - 1);
    const auto& b = *next;
    auto u = std::clamp((seconds-a.seconds)/(b.seconds-a.seconds), 0.0, 1.0);
    if (a.easing == QStringLiteral("hold")) u = 0.0;
    else if (a.easing == QStringLiteral("smooth")) u = u*u*(3.0-2.0*u);
    else if (a.easing == QStringLiteral("smoother")) u = u*u*u*(u*(u*6.0-15.0)+10.0);
    return a.value + (b.value-a.value)*u;
  }
};

struct RigNode {
  QString id;
  int parent = -1;
  QPainterPath path;
  QBrush fill;
  QPen pen{Qt::NoPen};
  QString text;
  QFont font;
  QRectF text_rect;
  bool centered = false;
  RigChannel x, y, rotation, sx{1.0, {}}, sy{1.0, {}}, opacity{1.0, {}};
  RigChannel trim{1.0, {}};
  QPointF pivot;
  QPainter::CompositionMode blend = QPainter::CompositionMode_SourceOver;
};

struct PreparedRigScene {
  double width = 0.0, height = 0.0;
  QVector<RigNode> nodes;
  QImage paper;
  double paper_opacity = 0.0;
};

inline bool rig_number(const QVariant& value, double* result) {
  const auto type = value.metaType().id();
  if (type != QMetaType::Double && type != QMetaType::Int &&
      type != QMetaType::LongLong && type != QMetaType::UInt &&
      type != QMetaType::ULongLong) return false;
  const auto number = value.toDouble();
  if (!std::isfinite(number) || std::abs(number) > 131072.0) return false;
  *result = number;
  return true;
}

inline std::shared_ptr<const PreparedRigScene> prepare_rig_scene(
    const QVariantMap& scene, QString* error = nullptr) {
  const auto fail = [&](const QString& reason) -> std::shared_ptr<const PreparedRigScene> {
    if (error) *error = reason;
    return {};
  };
  if (error) error->clear();
  auto out = std::make_shared<PreparedRigScene>();
  if (scene.value("schema") != QStringLiteral("looksy-rig-scene-v1") ||
      !rig_number(scene.value("width"), &out->width) ||
      !rig_number(scene.value("height"), &out->height) ||
      out->width <= 0 || out->height <= 0 || out->width > 32768 || out->height > 32768)
    return fail(QStringLiteral("A rig scene requires its v1 schema and bounded positive dimensions."));
  const auto nodes = scene.value("nodes").toList();
  if (nodes.isEmpty() || nodes.size() > 2048)
    return fail(QStringLiteral("A rig scene requires 1 to 2048 ordered nodes."));
  QMap<QString, int> identities;
  qsizetype key_count = 0, command_count = 0;
  const auto channel = [&](const QVariantMap& node, const QString& key, RigChannel& target) {
    if (!node.contains(key)) return true;
    const auto value = node.value(key);
    if (value.metaType().id() != QMetaType::QVariantList)
      return rig_number(value, &target.value);
    const auto list = value.toList();
    if (list.isEmpty()) return false;
    auto previous = -1.0;
    for (const auto& entry : list) {
      const auto parts = entry.toList();
      RigKey frame;
      if (parts.size() < 2 || parts.size() > 3 ||
          !rig_number(parts[0], &frame.seconds) || !rig_number(parts[1], &frame.value) ||
          frame.seconds < 0 || frame.seconds <= previous || frame.seconds > 3600) return false;
      frame.easing = parts.size() == 3 ? parts[2].toString() : QStringLiteral("smooth");
      if (frame.easing != "linear" && frame.easing != "hold" &&
          frame.easing != "smooth" && frame.easing != "smoother") return false;
      previous = frame.seconds;
      target.keys.append(frame);
      if (++key_count > 100000) return false;
    }
    return true;
  };
  for (const auto& entry : nodes) {
    const auto node = entry.toMap();
    RigNode n;
    n.id = node.value("id").toString();
    if (n.id.isEmpty() || n.id.size() > 128 || identities.contains(n.id))
      return fail(QStringLiteral("Rig node IDs must be unique and nonempty."));
    const auto parent = node.value("parent").toString();
    if (!parent.isEmpty()) {
      if (!identities.contains(parent))
        return fail(QStringLiteral("Parents must precede their children; cycles are not permitted."));
      n.parent = identities.value(parent);
    }
    if (!channel(node, "x", n.x) || !channel(node, "y", n.y) ||
        !channel(node, "rotation", n.rotation) || !channel(node, "scaleX", n.sx) ||
        !channel(node, "scaleY", n.sy) || !channel(node, "opacity", n.opacity) ||
        !channel(node, "trim", n.trim))
      return fail(QStringLiteral("Rig channels require finite numbers or ordered bounded time/value/easing keys."));
    for (const auto* bounded : {&n.opacity, &n.trim}) {
      if (bounded->value < 0 || bounded->value > 1) return fail("Opacity and trim must be in [0, 1].");
      for (const auto& key : bounded->keys)
        if (key.value < 0 || key.value > 1) return fail("Opacity and trim keys must be in [0, 1].");
    }
    for (const auto* scale : {&n.sx, &n.sy}) {
      if (std::abs(scale->value) > 100) return fail("Rig scales are limited to 100.");
      for (const auto& key : scale->keys)
        if (std::abs(key.value) > 100) return fail("Rig scale keys are limited to 100.");
    }
    const auto pivot = node.value("pivot", QVariantList{0.0, 0.0}).toList();
    double px = 0, py = 0;
    if (pivot.size() != 2 || !rig_number(pivot[0], &px) || !rig_number(pivot[1], &py))
      return fail("Rig pivots need two finite coordinates.");
    n.pivot = {px, py};
    const auto kind = node.value("kind", "group").toString();
    if (kind == "path") {
      const auto commands = node.value("commands").toList();
      if (commands.isEmpty() || commands.first().toList().value(0).toString() != "M")
        return fail("Rig paths must begin with M.");
      for (const auto& value : commands) {
        if (++command_count > 40000) return fail("Rig scene exceeds its path command budget.");
        const auto c = value.toList();
        const auto verb = c.value(0).toString();
        const auto count = verb == "M" || verb == "L" ? 2 : verb == "C" ? 6 :
                           verb == "Q" ? 4 : verb == "Z" ? 0 : -1;
        if (count < 0 || c.size() != count+1) return fail("Invalid rig path command.");
        double p[6]{};
        for (int j = 0; j < count; ++j)
          if (!rig_number(c[j+1], &p[j])) return fail("Invalid rig path coordinate.");
        if (verb == "M") n.path.moveTo(p[0], p[1]);
        else if (verb == "L") n.path.lineTo(p[0], p[1]);
        else if (verb == "Q") n.path.quadTo(p[0], p[1], p[2], p[3]);
        else if (verb == "C") n.path.cubicTo(p[0], p[1], p[2], p[3], p[4], p[5]);
        else n.path.closeSubpath();
      }
    } else if (kind == "ellipse" || kind == "rect" || kind == "text") {
      const auto box = node.value("rect").toList();
      double r[4]{};
      if (box.size() != 4) return fail("Rig rectangles need four numbers.");
      for (int j = 0; j < 4; ++j)
        if (!rig_number(box[j], &r[j])) return fail("Invalid rig rectangle.");
      if (r[2] < 0 || r[3] < 0) return fail("Rig rectangle dimensions cannot be negative.");
      const QRectF rect(r[0], r[1], r[2], r[3]);
      if (kind == "ellipse") n.path.addEllipse(rect);
      else if (kind == "rect") {
        double radius = 0;
        if (node.contains("radius") && !rig_number(node.value("radius"), &radius))
          return fail("Invalid corner radius.");
        if (radius < 0) return fail("Corner radius cannot be negative.");
        n.path.addRoundedRect(rect, radius, radius);
      } else {
        n.text = node.value("text").toString();
        double font_size = 24;
        if (n.text.size() > 4096 || (node.contains("fontSize") &&
            !rig_number(node.value("fontSize"), &font_size)) || font_size < 1 || font_size > 2048)
          return fail("Invalid rig typography.");
        n.font = QFont(node.value("font", "Georgia").toString());
        n.font.setPixelSize(static_cast<int>(font_size));
        n.font.setBold(node.value("bold", false).toBool());
        n.font.setLetterSpacing(QFont::AbsoluteSpacing, node.value("tracking", 0.0).toDouble());
        n.centered = node.value("center", false).toBool();
        n.text_rect = rect;
      }
    } else if (kind != "group") return fail("Unsupported rig node kind.");
    const QColor fill(node.value("fill", "#00000000").toString());
    const QColor stroke(node.value("stroke", "#00000000").toString());
    double stroke_width = 1;
    if (!fill.isValid() || !stroke.isValid() ||
        (node.contains("strokeWidth") && !rig_number(node.value("strokeWidth"), &stroke_width)) ||
        stroke_width < 0 || stroke_width > 4096) return fail("Invalid rig fill or stroke.");
    n.fill = QBrush(fill);
    n.pen = stroke.alpha() == 0 || stroke_width == 0 ? QPen(Qt::NoPen) :
        QPen(stroke, stroke_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    if (node.contains("gradient")) {
      const auto gradient = node.value("gradient").toMap();
      const auto coords = gradient.value("coordinates").toList();
      const auto radial = gradient.value("type") == "radial";
      double c[4]{};
      if (coords.size() != (radial ? 3 : 4)) return fail("Invalid gradient coordinates.");
      for (qsizetype j = 0; j < coords.size(); ++j)
        if (!rig_number(coords[j], &c[j])) return fail("Invalid gradient coordinate.");
      if (radial && c[2] <= 0) return fail("Gradient radius must be positive.");
      QGradient g = radial ? QGradient(QRadialGradient(c[0], c[1], c[2])) :
                            QGradient(QLinearGradient(c[0], c[1], c[2], c[3]));
      const auto stops = gradient.value("stops").toList();
      if (stops.size() < 2 || stops.size() > 16) return fail("A gradient needs 2 to 16 stops.");
      auto previous = -1.0;
      for (const auto& s : stops) {
        const auto pair = s.toList();
        double position = 0;
        const QColor color(pair.value(1).toString());
        if (pair.size() != 2 || !rig_number(pair[0], &position) ||
            position < 0 || position > 1 || position <= previous || !color.isValid())
          return fail("Invalid ordered gradient stop.");
        g.setColorAt(position, color);
        previous = position;
      }
      n.fill = QBrush(g);
    }
    const auto blend = node.value("blend", "normal").toString();
    if (blend == "screen") n.blend = QPainter::CompositionMode_Screen;
    else if (blend == "multiply") n.blend = QPainter::CompositionMode_Multiply;
    else if (blend != "normal") return fail("Unsupported rig blend mode.");
    identities.insert(n.id, static_cast<int>(out->nodes.size()));
    out->nodes.append(std::move(n));
  }
  if (scene.contains("paperOpacity") && !rig_number(scene.value("paperOpacity"), &out->paper_opacity))
    return fail("Invalid paper opacity.");
  if (out->paper_opacity < 0 || out->paper_opacity > 1) return fail("Invalid paper opacity.");
  if (out->paper_opacity > 0) {
    out->paper = QImage(512, 512, QImage::Format_RGB32);
    QRandomGenerator rng(7391U);
    for (int y = 0; y < 512; ++y) {
      auto* row = reinterpret_cast<QRgb*>(out->paper.scanLine(y));
      for (int x = 0; x < 512; ++x) {
        const auto v = static_cast<int>(rng.bounded(150U, 256U));
        row[x] = qRgb(v, v, v);
      }
    }
  }
  return out;
}

inline QImage render_rig_scene_frame(const QSize& size, const PreparedRigScene& scene, double seconds) {
  if (size.isEmpty() || static_cast<qint64>(size.width())*size.height() > 64LL*1024LL*1024LL ||
      !std::isfinite(seconds)) return {};
  QImage image(size, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  QTransform view;
  view.scale(size.width()/scene.width, size.height()/scene.height);
  QVector<QTransform> world;
  QVector<double> opacities;
  world.reserve(scene.nodes.size());
  opacities.reserve(scene.nodes.size());
  for (const auto& n : scene.nodes) {
    const auto parent = n.parent < 0 ? QTransform{} : world[n.parent];
    QTransform local;
    local.translate(n.x.at(seconds), n.y.at(seconds));
    local.translate(n.pivot.x(), n.pivot.y());
    local.rotate(n.rotation.at(seconds));
    local.scale(n.sx.at(seconds), n.sy.at(seconds));
    local.translate(-n.pivot.x(), -n.pivot.y());
    const auto matrix = local*parent;
    const auto alpha = n.opacity.at(seconds)*(n.parent < 0 ? 1.0 : opacities[n.parent]);
    world.append(matrix);
    opacities.append(alpha);
    if (alpha <= 0.00001) continue;
    painter.setWorldTransform(matrix*view);
    painter.setOpacity(alpha);
    painter.setCompositionMode(n.blend);
    painter.setPen(n.pen);
    painter.setBrush(n.fill);
    if (!n.text.isEmpty()) {
      painter.setFont(n.font);
      painter.setPen(QPen(n.fill, 1.0));
      painter.drawText(n.text_rect, (n.centered ? Qt::AlignHCenter : Qt::AlignLeft) | Qt::AlignVCenter, n.text);
    } else {
      const auto trim = n.trim.at(seconds);
      if (trim >= 1.0) painter.drawPath(n.path);
      else if (trim > 0.0) painter.drawPath(n.path.trimmed(0.0, trim));
    }
  }
  if (!scene.paper.isNull()) {
    painter.resetTransform();
    painter.setOpacity(scene.paper_opacity);
    painter.setCompositionMode(QPainter::CompositionMode_Multiply);
    painter.fillRect(image.rect(), QBrush(scene.paper));
  }
  return image;
}

}  // namespace photo_editor::desktop::timeline_motion
