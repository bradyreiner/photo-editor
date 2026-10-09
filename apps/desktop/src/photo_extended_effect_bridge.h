#pragma once

#include "photo_editor/pipeline/photo_effect_registry.h"
#include "photo_editor/pipeline/photo_effect_types.h"
#include "working_image.h"

#include <QBuffer>
#include <QColor>
#include <QCache>
#include <mutex>
#include <QCryptographicHash>
#include <QImageReader>
#include <QVariantMap>
#include <cmath>

namespace photo_editor::desktop {

inline bool extended_photo_effect(std::string_view type) {
  return pipeline::is_optical_photo_effect(type) || pipeline::is_artistic_photo_effect(type);
}

inline QVariantList extended_photo_effect_fields(const QString& type) {
  const auto id = type.toStdString();
  const auto fields = pipeline::is_optical_photo_effect(id)
      ? pipeline::optical_photo_effect_fields(id) : pipeline::artistic_photo_effect_fields(id);
  const pipeline::PhotoEffectInstance defaults;
  const auto values = pipeline::is_optical_photo_effect(id)
      ? pipeline::optical_photo_effect_values(id, defaults.optical)
      : pipeline::artistic_photo_effect_values(id, defaults.artistic);
  QVariantList result;
  for (const auto& field : fields) {
    const auto key = QString::fromUtf8(field.key.data(), static_cast<qsizetype>(field.key.size()));
    auto label = QString::fromUtf8(field.label.data(), static_cast<qsizetype>(field.label.size()));
    const bool fraction = label.contains(QStringLiteral("fraction"));
    label.replace(QStringLiteral("document px"), QStringLiteral("px"));
    label.replace(QStringLiteral("degrees"), QStringLiteral("°"));
    label.replace(QStringLiteral("frame fraction"), QStringLiteral("%"));
    label.replace(QStringLiteral("short-side fraction"), QStringLiteral("%"));
    QVariantMap row{{"key", key}, {"label", label},
        {"minimum", field.minimum}, {"maximum", field.maximum}, {"step", field.step},
        {"defaultValue", values.at(std::string(field.key))}};
    if (fraction) row.insert("scale", 100);
    QVariantList choices;
    const auto choice = [&choices](const char* label, int value) {
      choices.push_back(QVariantMap{{"label", QString::fromUtf8(label)}, {"value", value}});
    };
    if (id == "dither" && field.key == "mode") { choice("Ordered Bayer", 0); choice("Floyd–Steinberg", 1); }
    if (id == "dither" && field.key == "matrix_size") { choice("2 × 2", 2); choice("4 × 4", 4); choice("8 × 8", 8); }
    if (id == "pixel_mosaic" && field.key == "shape") { choice("Square", 0); choice("Hexagonal", 1); choice("Irregular", 2); }
    if (id == "displacement_map" && field.key == "edge_mode") { choice("Extend edges", 0); choice("Transparent edges", 1); }
    if (!choices.isEmpty()) row.insert("choices", choices);
    result.push_back(row);
  }
  return result;
}

inline QVariantMap extended_photo_effect_values(const pipeline::PhotoEffectInstance& effect,
    const QString& working_profile, const QVariantMap& layer) {
  QVariantMap result;
  if (!extended_photo_effect(effect.type_id)) return result;
  result.insert("extended", true);
  if (const auto* descriptor = pipeline::find_photo_effect(effect.type_id, effect.algorithm_version))
    result.insert("name", QString::fromUtf8(descriptor->display_name.data(), static_cast<qsizetype>(descriptor->display_name.size())));
  const auto values = pipeline::is_optical_photo_effect(effect.type_id)
      ? pipeline::optical_photo_effect_values(effect.type_id, effect.optical)
      : pipeline::artistic_photo_effect_values(effect.type_id, effect.artistic);
  for (const auto& [key, value] : values) result.insert(QString::fromStdString(key), value);
  if (effect.type_id == "gradient_map") {
    QVariantList stops;
    for (const auto& stop : effect.artistic.gradient_map.stops) {
      WorkingImage swatch{1, 1, {{stop.linear_rgb[0], stop.linear_rgb[1], stop.linear_rgb[2], 1.0F}}, {}};
      const auto image = qimage_from_working_image(swatch, QColorSpace::SRgb, QImage::Format_ARGB32, working_profile);
      stops.push_back(QVariantMap{{"position", stop.position},
          {"color", image.isNull() ? QStringLiteral("#000000") : image.pixelColor(0, 0).name()},
          {"linearRgb", QVariantList{stop.linear_rgb[0], stop.linear_rgb[1], stop.linear_rgb[2]}}});
    }
    result.insert("gradientStops", stops);
  }
  if (effect.type_id == "displacement_map") {
    const auto ref = QString::fromStdString(effect.artistic.displacement_map.asset_ref);
    result.insert("mapName", layer.value("effectMapAssets").toMap().value(ref).toMap().value("name"));
    result.insert("mapConfigured", !ref.isEmpty());
  }
  return result;
}

inline bool set_extended_photo_effect_values(pipeline::PhotoEffectInstance* effect,
    const QVariantMap& parameters, const QString& working_profile) {
  if (!effect || !extended_photo_effect(effect->type_id)) return false;
  const auto fields = pipeline::is_optical_photo_effect(effect->type_id)
      ? pipeline::optical_photo_effect_fields(effect->type_id) : pipeline::artistic_photo_effect_fields(effect->type_id);
  pipeline::PhotoEffectParameterValues values;
  for (const auto& field : fields) {
    const auto key = QString::fromUtf8(field.key.data(), static_cast<qsizetype>(field.key.size()));
    if (!parameters.contains(key)) continue;
    bool ok = false;
    const auto number = parameters.value(key).toDouble(&ok);
    if (!ok || !std::isfinite(number)) return false;
    values.emplace(field.key, number);
  }
  if (pipeline::is_optical_photo_effect(effect->type_id))
    return pipeline::set_optical_photo_effect_values(effect->type_id, values, &effect->optical);
  auto candidate = effect->artistic;
  if (!pipeline::set_artistic_photo_effect_values(effect->type_id, values, &candidate)) return false;
  if (effect->type_id == "gradient_map" && parameters.contains("gradientStops")) {
    const auto stops = parameters.value("gradientStops").toList();
    if (stops.size() < 2 || stops.size() > 32) return false;
    candidate.gradient_map.stops.clear();
    for (const auto& value : stops) {
      const auto stop = value.toMap();
      bool ok = false;
      const auto position = stop.value("position").toDouble(&ok);
      if (!ok || !std::isfinite(position) || position < 0 || position > 1) return false;
      std::array<float, 3> rgb{};
      if (stop.contains("linearRgb")) {
        const auto channels = stop.value("linearRgb").toList();
        if (channels.size() != 3) return false;
        for (int channel = 0; channel < 3; ++channel) {
          const auto number = channels.at(channel).toDouble(&ok);
          if (!ok || !std::isfinite(number) || std::abs(number) > 64) return false;
          rgb[static_cast<std::size_t>(channel)] = static_cast<float>(number);
        }
      } else {
        const QColor color(stop.value("color").toString());
        if (!color.isValid()) return false;
        QImage image(1, 1, QImage::Format_RGBA8888);
        image.fill(color); image.setColorSpace(QColorSpace::SRgb);
        const auto swatch = working_image_from_qimage(image, working_profile);
        if (swatch.isNull()) return false;
        rgb = {swatch.pixels[0].red, swatch.pixels[0].green, swatch.pixels[0].blue};
      }
      candidate.gradient_map.stops.push_back({position, rgb});
    }
    std::stable_sort(candidate.gradient_map.stops.begin(), candidate.gradient_map.stops.end(),
        [](const auto& a, const auto& b) { return a.position < b.position; });
  }
  effect->artistic = std::move(candidate);
  return true;
}

inline bool bind_photo_effect_maps(std::vector<pipeline::PhotoEffectInstance>* effects,
    const QVariantMap& layer, QString* error) {
  struct CachedMap { int width; int height; std::shared_ptr<const std::vector<float>> rgba; };
  static QCache<QString, CachedMap> cache(64 * 1024); // KiB; maps larger than 64 MiB are not retained.
  static std::mutex cache_mutex;
  const auto assets = layer.value("effectMapAssets").toMap();
  for (auto& effect : *effects) {
    auto& map = effect.artistic.displacement_map;
    if (effect.type_id != "displacement_map" || !effect.enabled || effect.amount == 0 || map.asset_ref.empty()) continue;
    const auto ref = QString::fromStdString(map.asset_ref);
    const auto bytes = QByteArray::fromBase64(assets.value(ref).toMap().value("png").toString().toLatin1());
    const auto digest = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    if (bytes.isEmpty() || digest != ref) {
      if (error) *error = QStringLiteral("Displacement map content failed verification.");
      return false;
    }
    {
      const std::scoped_lock lock(cache_mutex);
      if (const auto* cached = cache.object(ref); cached && cached->width == map.width && cached->height == map.height) {
        map.rgba = cached->rgba;
        continue;
      }
    }
    const auto image = QImage::fromData(bytes, "PNG").convertToFormat(QImage::Format_RGBA8888);
    if (bytes.isEmpty() || digest != ref || image.isNull() || image.width() != map.width || image.height() != map.height) {
      if (error) *error = QStringLiteral("Displacement map is missing or its stored content failed verification.");
      return false;
    }
    auto data = std::make_shared<std::vector<float>>(static_cast<std::size_t>(image.width()) * static_cast<std::size_t>(image.height()) * 4);
    for (int y = 0; y < image.height(); ++y) {
      const auto* row = image.constScanLine(y);
      for (int x = 0; x < image.width(); ++x) for (int c = 0; c < 4; ++c)
        (*data)[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width()) + static_cast<std::size_t>(x)) * 4 + static_cast<std::size_t>(c)] = c < 2
            ? (row[x * 4 + c] <= 128 ? row[x * 4 + c] / 256.0F
                : 0.5F + (row[x * 4 + c] - 128) / 254.0F)
            : row[x * 4 + c] / 255.0F;
    }
    map.rgba = std::move(data);
    const auto cost = (map.rgba->size() * sizeof(float) + 1023) / 1024;
    if (cost <= 64 * 1024) {
      const std::scoped_lock lock(cache_mutex);
      cache.insert(ref, new CachedMap{map.width, map.height, map.rgba}, static_cast<int>(cost));
    }
  }
  return true;
}
}  // namespace photo_editor::desktop
