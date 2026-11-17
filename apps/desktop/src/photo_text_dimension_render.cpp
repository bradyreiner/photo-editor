#include "photo_text_dimension_render.h"

#include "photo_text_dimension_mesh.h"
#include "photo_text_dimension_outline.h"
#include "photo_text_rasterizer.h"
#include "photo_layer_effect_codec.h"
#include "photo_editor/pipeline/photo_effects.h"
#include "photo_editor/pipeline/layer_styles.h"

#include <QColorSpace>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QPolygonF>
#include <QtGui/qrgbafloat.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace photo_editor::desktop {
namespace {
using Settings = PhotoTextDimensionSettings;
constexpr qint64 kMaximumFramePixels = 16000000;
constexpr qsizetype kMaximumMeshTriangles = 500000;

std::optional<Settings> settings_for(const QVariantMap& layer, QString* error) {
  if (!layer.contains("textDimension")) return Settings{};
  if (layer.value("textDimension").metaType().id() != QMetaType::QVariantMap) {
    if (error) *error = QStringLiteral("Invalid dimensional text settings.");
    return {};
  }
  return decode_photo_text_dimension_variant_map(layer.value("textDimension").toMap(), error);
}

QSizeF authored_box(const QVariantMap& layer) {
  return {layer.value("pixelWidth", 512).toDouble(), layer.value("pixelHeight", 128).toDouble()};
}

PhotoTextDimensionCamera camera_for(const Settings& settings, QSizeF box) {
  PhotoTextDimensionCamera camera;
  camera.projection = settings.projection == Settings::Projection::Perspective
      ? PhotoTextDimensionProjection::Perspective : PhotoTextDimensionProjection::Orthographic;
  camera.pivot = QVector3D(static_cast<float>(box.width() * settings.pivot_x),
      static_cast<float>(box.height() * settings.pivot_y), 0);
  camera.screen_origin = {box.width() * settings.pivot_x, box.height() * settings.pivot_y};
  camera.rotation = QQuaternion::fromEulerAngles(static_cast<float>(settings.rotation_x),
      static_cast<float>(settings.rotation_y), static_cast<float>(settings.rotation_z));
  camera.camera_distance = settings.focal_length;
  camera.perspective_focal_length_pixels = settings.focal_length;
  camera.near_plane = std::max(0.1, settings.focal_length * 0.001);
  return camera;
}

PhotoTextDimensionRenderStyle surface_for(const Settings& settings) {
  PhotoTextDimensionRenderStyle style;
  style.front = {settings.front_color, settings.roughness, settings.metallic};
  style.back = style.side = {settings.side_color, settings.roughness, settings.metallic};
  style.bevel = {settings.bevel_color, settings.roughness, settings.metallic};
  const auto azimuth = settings.light_azimuth * std::acos(-1.0) / 180.0;
  const auto elevation = settings.light_elevation * std::acos(-1.0) / 180.0;
  style.light_direction = QVector3D(static_cast<float>(std::cos(azimuth) * std::cos(elevation)),
      static_cast<float>(std::sin(azimuth) * std::cos(elevation)), static_cast<float>(std::sin(elevation)));
  style.ambient = settings.ambient;
  style.key_light = settings.light_intensity;
  return style;
}

std::shared_ptr<const PhotoTextDimensionMesh> mesh_for(const QVariantMap& layer,
    const Settings& settings, QString* error) {
  QVariantMap identity;
  for (const auto* key : {"text", "textTypography", "fontFamily", "fontSize", "fontWeight", "textColor",
                         "textAlignment", "pixelWidth", "pixelHeight", "textOriginX", "textOriginY"})
    if (layer.contains(QLatin1String(key))) identity.insert(QLatin1String(key), layer.value(QLatin1String(key)));
  const auto dimension = encode_photo_text_dimension(settings);
  for (const auto* key : {"depth", "bevelWidth", "bevelSegments", "bevelProfile", "useTextColor", "frontColor"})
    identity.insert(QLatin1String(key), dimension.value(QLatin1String(key)).toVariant());
  const auto key = QString::fromLatin1(QCryptographicHash::hash(
      QJsonDocument(QJsonObject::fromVariantMap(identity)).toJson(QJsonDocument::Compact),
      QCryptographicHash::Sha256).toHex());
  static QMutex mutex;
  static QHash<QString, std::shared_ptr<const PhotoTextDimensionMesh>> cache;
  static QStringList order;
  {
    const QMutexLocker guard(&mutex);
    if (cache.contains(key)) return cache.value(key);
  }
  const auto outlines = photo_text_dimension_outlines(layer);
  if (!outlines.error.isEmpty()) { if (error) *error = outlines.error; return {}; }
  auto result = std::make_shared<PhotoTextDimensionMesh>();
  for (const auto& face : outlines.faces) {
    PhotoTextDimensionGeometryOptions options;
    options.depth = settings.depth;
    options.bevel_width = settings.bevel_width;
    options.bevel_segments = settings.bevel_segments;
    options.bevel_profile = settings.bevel_profile == Settings::BevelProfile::Rounded
        ? PhotoTextDimensionBevelProfile::Rounded : PhotoTextDimensionBevelProfile::Chamfer;
    options.front_color = settings.use_text_color ? face.color : settings.front_color;
    options.max_triangles = static_cast<std::size_t>(kMaximumMeshTriangles);
    auto piece = build_photo_text_dimension_mesh(face.path, options);
    if (!piece.error.isEmpty()) { if (error) *error = piece.error; return {}; }
    if (result->triangles.size() + piece.triangles.size() > static_cast<std::size_t>(kMaximumMeshTriangles)) {
      if (error) *error = QStringLiteral("This 3D text exceeds the geometry budget. Use shorter text or a simpler bevel.");
      return {};
    }
    if (result->triangles.empty()) { result->lower = piece.lower; result->upper = piece.upper; }
    else {
      result->lower = {std::min(result->lower.x(), piece.lower.x()), std::min(result->lower.y(), piece.lower.y()), std::min(result->lower.z(), piece.lower.z())};
      result->upper = {std::max(result->upper.x(), piece.upper.x()), std::max(result->upper.y(), piece.upper.y()), std::max(result->upper.z(), piece.upper.z())};
    }
    result->triangles.insert(result->triangles.end(), std::make_move_iterator(piece.triangles.begin()), std::make_move_iterator(piece.triangles.end()));
  }
  // Empty text is a valid editable source with no visible geometry.
  if (result->triangles.empty()) return result;
  const auto bytes = result->triangles.size() * sizeof(PhotoTextDimensionTriangle);
  if (bytes <= 64ULL * 1024 * 1024) {
    const QMutexLocker guard(&mutex);
    if (!cache.contains(key)) order.push_back(key);
    cache.insert(key, result);
    auto cached_bytes = [&] { std::size_t sum = 0; for (const auto& value : cache) sum += value->triangles.size() * sizeof(PhotoTextDimensionTriangle); return sum; };
    while (order.size() > 4 || cached_bytes() > 64ULL * 1024 * 1024) cache.remove(order.takeFirst());
  }
  return result;
}

bool finite_rect(const QRectF& bounds) {
  return std::isfinite(bounds.left()) && std::isfinite(bounds.top()) &&
      std::isfinite(bounds.right()) && std::isfinite(bounds.bottom()) &&
      bounds.width() > 0 && bounds.height() > 0;
}

QRect checked_bounds(QRectF logical, QSizeF box, QPointF scale,
    const QTransform& plane, QPointF shadow, QString* error) {
  // A homography with a horizon through the content cannot be represented by a
  // finite raster. Check its linear denominator over the entire input rectangle.
  const std::array<QPointF, 4> corners{logical.topLeft(), logical.topRight(), logical.bottomRight(), logical.bottomLeft()};
  double sign = 0;
  for (const auto& point : corners) {
    const auto denominator = plane.m13() * point.x() + plane.m23() * point.y() + plane.m33();
    if (!std::isfinite(denominator) || std::abs(denominator) < 1e-6 || (sign != 0 && sign * denominator <= 0)) {
      if (error) *error = QStringLiteral("Perspective crosses the text plane's horizon. Reduce the corner distortion.");
      return {};
    }
    sign = denominator;
  }
  auto mapped = plane.mapRect(logical);
  mapped = mapped.united(mapped.translated(shadow));
  if (!finite_rect(mapped)) { if (error) *error = QStringLiteral("The projected text bounds are invalid."); return {}; }
  const auto w = box.width() * scale.x(), h = box.height() * scale.y();
  const auto left = std::ceil(std::max({0.0, -mapped.left() * scale.x(), mapped.right() * scale.x() - w})) + 2;
  const auto top = std::ceil(std::max({0.0, -mapped.top() * scale.y(), mapped.bottom() * scale.y() - h})) + 2;
  const auto width = std::ceil(w) + 2 * left, height = std::ceil(h) + 2 * top;
  if (!std::isfinite(width) || !std::isfinite(height) || width > 16384 || height > 16384 ||
      width * height > static_cast<double>(kMaximumFramePixels)) {
    if (error) *error = QStringLiteral("The projected text exceeds the image budget. Reduce depth, tilt or perspective.");
    return {};
  }
  return QRect(-static_cast<int>(left), -static_cast<int>(top), static_cast<int>(width), static_cast<int>(height));
}

QPointF shadow_for(const QVariantMap& layer) {
  const auto style = decode_photo_text_style(QJsonObject::fromVariantMap(layer.value("textTypography").toMap()));
  return style ? QPointF(style->shadow_x, style->shadow_y) : QPointF{};
}

std::array<float, 4> sample_premultiplied(const QImage& image, double x, double y) {
  if (!std::isfinite(x) || !std::isfinite(y) || x < -1 || y < -1 || x > image.width() || y > image.height()) return {};
  const auto x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
  std::array<float, 4> result{};
  for (int j = 0; j < 2; ++j) for (int i = 0; i < 2; ++i) {
    if (x0 + i < 0 || y0 + j < 0 || x0 + i >= image.width() || y0 + j >= image.height()) continue;
    const auto weight = static_cast<float>((i ? x - x0 : 1 - (x - x0)) * (j ? y - y0 : 1 - (y - y0)));
    const auto pixel = reinterpret_cast<const QRgbaFloat32*>(image.constScanLine(y0 + j))[x0 + i];
    result[0] += pixel.red() * pixel.alpha() * weight;
    result[1] += pixel.green() * pixel.alpha() * weight;
    result[2] += pixel.blue() * pixel.alpha() * weight;
    result[3] += pixel.alpha() * weight;
  }
  return result;
}

float linear_channel(double value) {
  return static_cast<float>(value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4));
}

bool preflight_finishing(const QVariantMap& layer, QSize content, QPointF scale, QString* error) {
  using namespace photo_editor::pipeline;
  const auto effects = decode_photo_layer_effects(layer, error);
  if (!effects) return false;
  PhotoEffectSupport support;
  const auto effect_admission = photo_effect_stack_support(*effects,
      {.scale_x = scale.x(), .scale_y = scale.y()}, &support);
  if (!effect_admission.ok()) { if (error) *error = QString::fromStdString(effect_admission.error); return false; }
  const auto width = static_cast<qint64>(content.width()) + 2LL * support.x;
  const auto height = static_cast<qint64>(content.height()) + 2LL * support.y;
  if (width <= 0 || height <= 0 || width > 16384 || height > 16384 || width * height > 32000000) {
    if (error) *error = QStringLiteral("The 3D text and its filters exceed the raster budget. Reduce the effect radius or text depth.");
    return false;
  }
  if (layer.contains("layerStyleStack")) {
    std::string detail;
    const auto styles = deserialize_layer_style_stack(layer.value("layerStyleStack").toString().toStdString(), &detail);
    if (!styles) { if (error) *error = QString::fromStdString(detail); return false; }
    LayerStyleRenderOptions options;
    options.document_scale_x = scale.x(); options.document_scale_y = scale.y();
    options.global_light_angle_degrees = layer.value("_documentLayerStyleLightAngle",
        layer.value("layerStyleGlobalLightAngle", -45.0)).toDouble();
    const auto style_admission = preflight_layer_style_stack(static_cast<int>(width), static_cast<int>(height), *styles, options);
    if (!style_admission.ok()) { if (error) *error = QString::fromStdString(style_admission.error); return false; }
  }
  return true;
}
}  // namespace

bool photo_text_dimension_is_active(const QVariantMap& layer) {
  if (!layer.contains("textDimension")) return false;
  const auto settings = settings_for(layer, nullptr);
  return !settings || settings->enabled || settings->corner_pin_enabled;
}

QTransform photo_text_dimension_corner_transform(const Settings& settings, QSizeF box, bool* valid) {
  QPolygonF from{{0, 0}, {box.width(), 0}, {box.width(), box.height()}, {0, box.height()}};
  QPolygonF to;
  for (const auto& point : settings.corner_pins) to << QPointF(point.x() * box.width(), point.y() * box.height());
  QTransform result;
  const auto okay = QTransform::quadToQuad(from, to, result);
  if (valid) *valid = okay;
  return result;
}

QTransform photo_text_dimension_plane_transform(const QVariantMap& layer, const Settings& settings, QSizeF box, bool* valid) {
  // Direct pinning is an explicit alternative to the legacy two-slider plane.
  if (settings.corner_pin_enabled) return photo_text_dimension_corner_transform(settings, box, valid);
  const auto horizontal = box.width() * 0.32 * layer.value("textPerspectiveHorizontal").toDouble() / 100.0;
  const auto vertical = box.height() * 0.32 * layer.value("textPerspectiveVertical").toDouble() / 100.0;
  QPolygonF from{{0, 0}, {box.width(), 0}, {box.width(), box.height()}, {0, box.height()}};
  QPolygonF to{{horizontal, vertical}, {box.width() - horizontal, -vertical},
      {box.width() + horizontal, box.height() - vertical}, {-horizontal, box.height() + vertical}};
  const auto bounds = to.boundingRect();
  for (auto& point : to) point = {(point.x() - bounds.left()) * box.width() / bounds.width(),
      (point.y() - bounds.top()) * box.height() / bounds.height()};
  QTransform result;
  const auto okay = QTransform::quadToQuad(from, to, result);
  if (valid) *valid = okay;
  return result;
}

QPointF project_photo_text_dimension_layer_point(const QVariantMap& layer,
    const Settings& settings, QVector3D point, bool* valid) {
  const auto box = authored_box(layer);
  bool okay = true;
  QPointF projected(point.x(), point.y());
  if (settings.enabled) projected = project_photo_text_dimension_point(
      point, camera_for(settings, box), {}, &okay);
  bool plane_valid = false;
  const auto plane = photo_text_dimension_plane_transform(layer, settings, box, &plane_valid);
  projected = plane.map(projected);
  okay = okay && plane_valid && std::isfinite(projected.x()) && std::isfinite(projected.y());
  if (valid) *valid = okay;
  return projected;
}

QRectF preflight_photo_text_dimension(const QVariantMap& layer, QSize raster_size, QString* error) {
  if (error) error->clear();
  const auto settings = settings_for(layer, error);
  if (!settings) return {};
  const auto box = authored_box(layer);
  if (!raster_size.isValid()) raster_size = box.toSize();
  if (box.width() < 16 || box.height() < 16 || !std::isfinite(box.width()) || !std::isfinite(box.height()) ||
      raster_size.width() <= 0 || raster_size.height() <= 0) {
    if (error) *error = QStringLiteral("Invalid dimensional text frame."); return {};
  }
  if (!settings->enabled && !settings->corner_pin_enabled) return QRectF(QPointF{}, box);
  QRectF projected(QPointF{}, box);
  if (settings->enabled) {
    const auto mesh = mesh_for(layer, *settings, error);
    if (!mesh) return {};
    if (!mesh->triangles.empty()) {
      PhotoTextDimensionRenderOptions options;
      options.reference_rect = projected;
      options.output_scale = {raster_size.width() / box.width(), raster_size.height() / box.height()};
      const auto admission = preflight_photo_text_dimension_mesh(*mesh, camera_for(*settings, box), options);
      if (!admission.error.isEmpty()) { if (error) *error = admission.error; return {}; }
      projected = QRectF(admission.pixel_bounds.x() / options.output_scale.x(), admission.pixel_bounds.y() / options.output_scale.y(),
          admission.pixel_bounds.width() / options.output_scale.x(), admission.pixel_bounds.height() / options.output_scale.y());
    }
  }
  bool valid = false;
  const auto plane = photo_text_dimension_plane_transform(layer, *settings, box, &valid);
  if (!valid) { if (error) *error = QStringLiteral("The text perspective cannot be mapped."); return {}; }
  const QPointF scale(raster_size.width() / box.width(), raster_size.height() / box.height());
  if (!settings->enabled && plane.isIdentity()) {
    if (!preflight_finishing(layer, raster_size, scale, error)) return {};
    return QRectF(QPointF{}, box);
  }
  const auto bounds = checked_bounds(projected, box, scale, plane, settings->enabled ? shadow_for(layer) : QPointF{}, error);
  if (bounds.isValid() && !preflight_finishing(layer, bounds.size(), scale, error)) return {};
  return bounds.isValid() ? QRectF(bounds.x() / scale.x(), bounds.y() / scale.y(), bounds.width() / scale.x(), bounds.height() / scale.y()) : QRectF{};
}

PhotoTextDimensionFrame render_photo_text_dimension(const QVariantMap& layer, QSize raster_size) {
  PhotoTextDimensionFrame result;
  const auto settings = settings_for(layer, &result.error);
  if (!settings) return result;
  const auto box = authored_box(layer);
  if (!raster_size.isValid()) raster_size = box.toSize();
  result.logical_bounds = preflight_photo_text_dimension(layer, raster_size, &result.error);
  if (!result.error.isEmpty() || !result.logical_bounds.isValid()) return result;
  const QPointF scale(raster_size.width() / box.width(), raster_size.height() / box.height());
  QImage source;
  QPoint origin;
  if (settings->enabled) {
    const auto mesh = mesh_for(layer, *settings, &result.error);
    if (!mesh) return result;
    if (!mesh->triangles.empty()) {
      PhotoTextDimensionRenderOptions options;
      options.reference_rect = QRectF(QPointF{}, box); options.output_scale = scale;
      auto rendered = render_photo_text_dimension_mesh(*mesh, camera_for(*settings, box), surface_for(*settings), options);
      if (!rendered.error.isEmpty()) { result.error = rendered.error; return result; }
      source = std::move(rendered.image); origin = rendered.origin_offset;
    } else {
      source = QImage(raster_size, QImage::Format_RGBA32FPx4); source.fill(Qt::transparent);
      source.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
    }
  } else {
    source = rasterize_legacy_photo_text(layer, raster_size, &result.error);
    if (!result.error.isEmpty()) return result;
  }
  if (source.isNull()) { result.error = QStringLiteral("Could not prepare dimensional text pixels."); return result; }
  if (!source.colorSpace().isValid()) source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  source = source.convertedToColorSpace(QColorSpace(QColorSpace::SRgbLinear), QImage::Format_RGBA32FPx4);
  if (source.isNull()) { result.error = QStringLiteral("Could not preserve the dimensional text's linear colors."); return result; }
  const auto plane = photo_text_dimension_plane_transform(layer, *settings, box);
  if (!settings->enabled && plane.isIdentity()) {
    result.image = std::move(source); result.logical_bounds = QRectF(QPointF{}, box); return result;
  }
  const auto output_bounds = QRect(qRound(result.logical_bounds.x() * scale.x()), qRound(result.logical_bounds.y() * scale.y()),
      qRound(result.logical_bounds.width() * scale.x()), qRound(result.logical_bounds.height() * scale.y()));
  result.image = QImage(output_bounds.size(), QImage::Format_RGBA32FPx4);
  if (result.image.isNull()) { result.error = QStringLiteral("Could not allocate dimensional text output."); return result; }
  result.image.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
  result.padding = -output_bounds.topLeft();
  const auto inverse = plane.inverted();
  const auto shadow = settings->enabled ? shadow_for(layer) : QPointF{};
  const auto style = decode_photo_text_style(QJsonObject::fromVariantMap(layer.value("textTypography").toMap()));
  const auto shadow_color = style ? style->shadow_color : QColor(Qt::transparent);
  const std::array<float, 3> shadow_rgb{linear_channel(shadow_color.redF()), linear_channel(shadow_color.greenF()), linear_channel(shadow_color.blueF())};
  const auto sample = [&](QPointF logical) {
    const auto local = inverse.map(logical);
    return sample_premultiplied(source, local.x() * scale.x() - origin.x() - 0.5,
        local.y() * scale.y() - origin.y() - 0.5);
  };
  for (int y = 0; y < result.image.height(); ++y) {
    auto* row = reinterpret_cast<QRgbaFloat32*>(result.image.scanLine(y));
    for (int x = 0; x < result.image.width(); ++x) {
      const QPointF position((x + output_bounds.x() + 0.5) / scale.x(), (y + output_bounds.y() + 0.5) / scale.y());
      const auto foreground = sample(position);
      const auto shadow_alpha = shadow.isNull() ? 0.0F : sample(position - shadow)[3] * static_cast<float>(shadow_color.alphaF());
      const auto behind = shadow_alpha * (1 - foreground[3]);
      const auto alpha = foreground[3] + behind;
      row[x] = alpha > 0 ? QRgbaFloat32{(foreground[0] + shadow_rgb[0] * behind) / alpha,
          (foreground[1] + shadow_rgb[1] * behind) / alpha, (foreground[2] + shadow_rgb[2] * behind) / alpha, alpha}
          : QRgbaFloat32{0, 0, 0, 0};
    }
  }
  return result;
}
}  // namespace photo_editor::desktop
