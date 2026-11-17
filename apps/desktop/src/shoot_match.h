#pragma once

#include "edit_adjustment_patch.h"
#include "linked_image_source.h"
#include "photo_render_utils.h"
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDateTime>
#include <QDir>
#include <QUrl>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace photo_editor::desktop {

inline constexpr int shoot_match_maximum_targets = 64;

struct ShootMatchFeatures {
  double median = 0;
  double range = 0;
  double neutral_red = 0;
  double neutral_blue = 0;
  double chroma = 0;
  double clipped = 0;
  bool neutral_available = false;
  bool valid = false;
};

inline ShootMatchFeatures shoot_match_features(const QImage& image) {
  ShootMatchFeatures result;
  if (image.isNull()) return result;
  const auto pixels = image.convertToFormat(QImage::Format_ARGB32);
  std::vector<double> luminance;
  int neutral_count = 0;
  int clipped_count = 0;
  const auto linear = [](double value) {
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
  };
  for (int y = 0; y < pixels.height(); ++y) {
    const auto* row = reinterpret_cast<const QRgb*>(pixels.constScanLine(y));
    for (int x = 0; x < pixels.width(); ++x) {
      if (qAlpha(row[x]) < 250) continue;
      const double r = qRed(row[x]) / 255.0, g = qGreen(row[x]) / 255.0, b = qBlue(row[x]) / 255.0;
      const double maximum = std::max({r, g, b}), minimum = std::min({r, g, b});
      const double lr = linear(r), lg = linear(g), lb = linear(b);
      luminance.push_back(std::log2(std::max(1.0 / 4096, .2126 * lr + .7152 * lg + .0722 * lb)));
      result.chroma += maximum - minimum;
      clipped_count += maximum >= 254.0 / 255;
      if (minimum > .08 && maximum < .92 && maximum - minimum < .20) {
        const auto sum = std::max(.03, lr + lg + lb);
        result.neutral_red += (lr - lg) / sum;
        result.neutral_blue += (lb - lg) / sum;
        ++neutral_count;
      }
    }
  }
  if (luminance.size() < 64) return result;
  std::sort(luminance.begin(), luminance.end());
  result.median = luminance[luminance.size() / 2];
  result.range = luminance[luminance.size() * 9 / 10] - luminance[luminance.size() / 10];
  result.chroma /= double(luminance.size());
  result.clipped = double(clipped_count) / double(luminance.size());
  result.neutral_available = neutral_count >= 64 && double(neutral_count) / double(luminance.size()) >= .02;
  if (neutral_count > 0) {
    result.neutral_red /= neutral_count;
    result.neutral_blue /= neutral_count;
  }
  result.valid = true;
  return result;
}

inline double shoot_match_distance(const ShootMatchFeatures& reference, const ShootMatchFeatures& target,
                                   bool lighting, bool look) {
  const auto square = [](double value) { return value * value; };
  double distance = 0;
  if (lighting) {
    distance += square(reference.median - target.median);
    if (reference.neutral_available && target.neutral_available)
      distance += 4 * (square(reference.neutral_red - target.neutral_red) + square(reference.neutral_blue - target.neutral_blue));
  }
  if (look) distance += square((reference.range - target.range) / 4) + 4 * square(reference.chroma - target.chroma);
  return std::sqrt(distance);
}

inline QVariantMap shoot_match_source_stamp(const QString& path) {
  const QFileInfo info(path);
  return {{"revision", linked_image_file_revision(path)}, {"path", info.absoluteFilePath()}, {"bytes", info.size()},
          {"modified", info.lastModified().toMSecsSinceEpoch()}, {"exists", info.isFile() && info.isReadable()}};
}

inline QImage render_shoot_match_image(const QImage& source, const EditRecipeState& state,
                                      const photo_editor::pipeline::DecodedImageMetadata& metadata,
                                      const std::atomic_bool* cancellation) {
  return render_output_image_with_backend(source, state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly, &metadata, nullptr, {}, photo_editor::pipeline::RemovalQuality::Final,
      RenderOutputPolicy::DisplayReferred, nullptr, {.cancellation_requested = cancellation});
}

// Deterministic pixel-statistics matching. Similarity is an editing aid, never
// a model confidence score or a substitute for photographic review.
inline QVariantMap match_shoot_photo(const QVariantMap& asset, const ShootMatchFeatures& reference,
                                    const QVariantMap& options, const QString& preview_directory,
                                    int index, const std::atomic_bool* cancellation) {
  QElapsedTimer timer; timer.start();
  QVariantMap result{{"assetId", asset.value("assetId")}, {"filePath", asset.value("filePath")},
                     {"name", QFileInfo(asset.value("filePath").toString()).fileName()}, {"ok", false}};
  const auto failed = [&](const QString& error) { result.insert("error", error); return result; };
  const auto payload = asset.value("adjustmentPatchJson").toString();
  EditRecipeState before;
  if (!payload.trimmed().isEmpty()) {
    const auto parsed = parse_adjustment_payload_or_legacy_recipe(payload);
    if (!parsed.ok()) return failed(QStringLiteral("The saved edits could not be read."));
    before = *parsed.state;
  }
  const auto linked_revision = linked_photo_layer_sources_revision(before.layers);
  QImage source;
  photo_editor::pipeline::DecodedImageMetadata metadata;
  const auto path = asset.value("filePath").toString();
  if (!decode_source_image(path, &source, 160, before, &metadata, {.cancellation_requested = cancellation}))
    return failed(QStringLiteral("The source image could not be decoded."));
  const auto before_image = render_shoot_match_image(source, before, metadata, cancellation);
  const auto initial = shoot_match_features(before_image);
  if (!initial.valid) return failed(QStringLiteral("There are too few opaque pixels to compare this image."));
  const bool lighting = options.value("lighting").toBool();
  const bool look = options.value("look").toBool();
  const bool white_balance = reference.neutral_available && initial.neutral_available;
  auto best = before;
  auto best_features = initial;
  auto best_distance = shoot_match_distance(reference, initial, lighting, look);
  const auto initial_distance = best_distance;
  struct Parameter { double EditRecipeState::*member; double step; double radius; double minimum; double maximum; bool enabled; };
  const std::array<Parameter, 5> parameters{{
      {&EditRecipeState::exposure, .6, 2, -5, 5, lighting},
      {&EditRecipeState::temperature, 14, 35, -100, 100, lighting && white_balance},
      {&EditRecipeState::tint, 14, 35, -100, 100, lighting && white_balance},
      {&EditRecipeState::contrast, 20, 40, -100, 100, look},
      {&EditRecipeState::saturation, 20, 40, -100, 100, look},
  }};
  for (const double step_scale : {1.0, .5, .2, .08}) {
    for (const auto& parameter : parameters) {
      if (!parameter.enabled) continue;
      // Each candidate starts from the current best, while both directions
      // share the same center. Do not compound a positive probe into the negative.
      const auto center = best;
      for (const double direction : {-1.0, 1.0}) {
        if (cancellation->load(std::memory_order_relaxed)) return failed(QStringLiteral("Cancelled."));
        auto candidate = center;
        const auto original = before.*(parameter.member);
        candidate.*(parameter.member) = std::clamp(center.*(parameter.member) + direction * parameter.step * step_scale,
            std::min(original, std::max(parameter.minimum, original - parameter.radius)),
            std::max(original, std::min(parameter.maximum, original + parameter.radius)));
        const auto features = shoot_match_features(render_shoot_match_image(source, candidate, metadata, cancellation));
        if (!features.valid || (lighting && white_balance && !features.neutral_available)) continue;
        const auto distance = shoot_match_distance(reference, features, lighting, look);
        if (distance + 1e-6 < best_distance) { best = candidate; best_features = features; best_distance = distance; }
      }
    }
  }
  const auto blend = [](double current, double proposed, double strength) { return current + (proposed - current) * strength; };
  const double lighting_amount = options.value("lightingAmount").toDouble();
  const double look_amount = options.value("lookAmount").toDouble();
  best.exposure = blend(before.exposure, best.exposure, lighting_amount);
  best.temperature = blend(before.temperature, best.temperature, lighting_amount);
  best.tint = blend(before.tint, best.tint, lighting_amount);
  best.contrast = blend(before.contrast, best.contrast, look_amount);
  best.saturation = blend(before.saturation, best.saturation, look_amount);
  const auto fitted_image = render_shoot_match_image(source, best, metadata, cancellation);
  best_features = shoot_match_features(fitted_image);
  if (!best_features.valid || cancellation->load(std::memory_order_relaxed)) return failed(QStringLiteral("The preview did not finish."));
  best_distance = shoot_match_distance(reference, best_features, lighting, look);
  const auto after_payload = serialize_adjustment_patch_json(best, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
  const auto before_canonical = serialize_adjustment_patch_json(before, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
  const bool changed = after_payload != before_canonical;
  const bool outlier = best_distance > .5 || best_distance > initial_distance + .005 ||
      best_features.clipped > initial.clipped + .01 || std::abs(best.exposure - before.exposure) >= 1.8;
  const auto before_path = QDir(preview_directory).filePath(QStringLiteral("%1-before.png").arg(index));
  const auto after_path = QDir(preview_directory).filePath(QStringLiteral("%1-after.png").arg(index));
  QImage review_source;
  photo_editor::pipeline::DecodedImageMetadata review_metadata;
  if (!decode_source_image(path, &review_source, 640, before, &review_metadata, {.cancellation_requested = cancellation}))
    return failed(QStringLiteral("The comparison source could not be decoded."));
  const auto review_before = render_shoot_match_image(review_source, before, review_metadata, cancellation);
  const auto review_after = render_shoot_match_image(review_source, best, review_metadata, cancellation);
  if (review_before.isNull() || review_after.isNull() || !review_before.save(before_path) || !review_after.save(after_path))
    return failed(QStringLiteral("The comparison previews could not be saved."));
  QStringList warnings;
  if (lighting && !white_balance) warnings.append(QStringLiteral("No reliable neutral area; white balance was preserved."));
  if (outlier) warnings.append(QStringLiteral("Review closely: the remaining difference, clipping, or exposure change is large."));
  if (!changed) warnings.append(QStringLiteral("No improving adjustment was found."));
  if (linked_photo_layer_sources_revision(before.layers) != linked_revision)
    return failed(QStringLiteral("A linked layer source changed. Build previews again."));
  result.insert("linkedSourcesRevision", linked_revision);
  result.insert("ok", true); result.insert("changed", changed); result.insert("outlier", outlier);
  result.insert("recommended", changed && !outlier); result.insert("warning", warnings.join(QLatin1Char(' ')));
  result.insert("beforeSource", QUrl::fromLocalFile(before_path).toString());
  result.insert("afterSource", QUrl::fromLocalFile(after_path).toString());
  result.insert("beforeDistance", initial_distance); result.insert("afterDistance", best_distance);
  result.insert("elapsedMs", timer.elapsed()); result.insert("afterPatchJson", after_payload);
  result.insert("changes", QVariantMap{{"exposure", best.exposure - before.exposure},
      {"temperature", best.temperature - before.temperature}, {"tint", best.tint - before.tint},
      {"contrast", best.contrast - before.contrast}, {"saturation", best.saturation - before.saturation}});
  return result;
}

}  // namespace photo_editor::desktop
