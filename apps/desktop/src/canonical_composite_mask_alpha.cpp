#include "canonical_composite_mask_alpha.h"

#include "local_mask_compiler.h"
#include "working_image.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <ranges>
#include <utility>
#include <vector>

#include <QCryptographicHash>
#include <QJsonDocument>

namespace photo_editor::desktop {
namespace {

QString canonical_composite_mask_alpha_cache_key(
    const QImage& source_image,
    const QVariantList& masks,
    double exposure_gain,
    const std::array<double, 3>& white_balance) {
  QVariantMap payload;
  payload.insert(
      QStringLiteral("version"),
      QStringLiteral("canonical-engine-composite-mask-alpha-v1"));
  payload.insert(
      QStringLiteral("imageCacheKey"),
      QString::number(source_image.cacheKey()));
  payload.insert(QStringLiteral("width"), source_image.width());
  payload.insert(QStringLiteral("height"), source_image.height());
  payload.insert(QStringLiteral("format"), static_cast<int>(source_image.format()));
  payload.insert(
      QStringLiteral("sourceIccSha256"),
      QString::fromLatin1(
          QCryptographicHash::hash(
              source_image.colorSpace().iccProfile(),
              QCryptographicHash::Sha256)
              .toHex()));
  payload.insert(
      QStringLiteral("exposureGain"),
      QString::number(exposure_gain, 'g', 17));
  payload.insert(
      QStringLiteral("whiteBalanceR"),
      QString::number(white_balance[0], 'g', 17));
  payload.insert(
      QStringLiteral("whiteBalanceG"),
      QString::number(white_balance[1], 'g', 17));
  payload.insert(
      QStringLiteral("whiteBalanceB"),
      QString::number(white_balance[2], 'g', 17));
  payload.insert(QStringLiteral("masks"), masks);
  return QStringLiteral("sha1:%1").arg(QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument::fromVariant(payload).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha1)
          .toHex()));
}

struct LazyCanonicalMaskAlpha final {
  QImage source_image;
  QVariantList mask_maps;
  std::vector<photo_editor::pipeline::LocalMask> compiled_masks;
  double exposure_gain = 1.0;
  std::array<double, 3> white_balance = {1.0, 1.0, 1.0};
  std::vector<std::uint8_t> alpha;
  bool initialized = false;

  void initialize() {
    if (initialized) {
      return;
    }
    initialized = true;

    auto working = working_image_from_qimage(source_image);
    if (working.isNull() || working.width != source_image.width() ||
        working.height != source_image.height()) {
      return;
    }
    for (auto& pixel : working.pixels) {
      pixel.red = static_cast<float>(std::max(
          0.0,
          static_cast<double>(pixel.red) * exposure_gain * white_balance[0]));
      pixel.green = static_cast<float>(std::max(
          0.0,
          static_cast<double>(pixel.green) * exposure_gain * white_balance[1]));
      pixel.blue = static_cast<float>(std::max(
          0.0,
          static_cast<double>(pixel.blue) * exposure_gain * white_balance[2]));
    }

    photo_editor::pipeline::LocalMaskSet mask_set;
    mask_set.id = "thumbnail-composite";
    mask_set.name = "Thumbnail composite";
    if (!compiled_masks.empty()) {
      mask_set.sources = compiled_masks;
    } else {
      mask_set.sources.reserve(static_cast<std::size_t>(mask_maps.size()));
      for (const auto& value : mask_maps) {
        const auto mask = value.toMap();
        if (mask.isEmpty()) {
          continue;
        }
        mask_set.sources.push_back(compile_pipeline_local_mask(
            mask,
            LocalMaskRasterPolicy::RebuildEditableAtOutputResolution,
            source_image.size()));
      }
    }
    if (mask_set.sources.empty()) {
      return;
    }

    alpha = photo_editor::pipeline::rasterize_local_mask_set_alpha(
        mask_set,
        source_image.width(),
        source_image.height(),
        working.pixels,
        photo_editor::pipeline::default_color_pipeline_config());
  }
};

}  // namespace

MaskAlphaEvaluationResult evaluate_canonical_composite_mask_alpha(
    const QImage& source_image,
    const QVariantList& masks,
    double exposure_gain,
    const std::array<double, 3>& white_balance) {
  if (source_image.isNull() || source_image.width() <= 0 ||
      source_image.height() <= 0 || masks.isEmpty() ||
      !std::isfinite(exposure_gain) || exposure_gain < 0.0 ||
      !std::ranges::all_of(white_balance, [](double value) {
        return std::isfinite(value) && value >= 0.0;
      })) {
    return {};
  }

  const auto lazy_alpha = std::make_shared<LazyCanonicalMaskAlpha>(
      LazyCanonicalMaskAlpha{
          .source_image = source_image,
          .mask_maps = masks,
          .exposure_gain = exposure_gain,
          .white_balance = white_balance,
      });
  const auto expected_alpha_size =
      static_cast<std::size_t>(source_image.width()) *
      static_cast<std::size_t>(source_image.height());
  return evaluate_mask_alpha_cached(
      canonical_composite_mask_alpha_cache_key(
          source_image,
          masks,
          exposure_gain,
          white_balance),
      source_image.size(),
      static_cast<int>(masks.size()),
      [lazy_alpha, expected_alpha_size](int x, int y, double, double) {
        lazy_alpha->initialize();
        if (lazy_alpha->alpha.size() != expected_alpha_size) {
          return 0.0;
        }
        const auto index = static_cast<std::size_t>(
            y * lazy_alpha->source_image.width() + x);
        return static_cast<double>(lazy_alpha->alpha[index]) / 255.0;
      });
}

MaskAlphaEvaluationResult evaluate_canonical_composite_mask_alpha(
    const QString& cache_key,
    const QImage& source_image,
    const std::vector<photo_editor::pipeline::LocalMask>& masks,
    double exposure_gain,
    const std::array<double, 3>& white_balance) {
  if (cache_key.isEmpty() || source_image.isNull() ||
      source_image.width() <= 0 || source_image.height() <= 0 ||
      masks.empty() || !std::isfinite(exposure_gain) || exposure_gain < 0.0 ||
      !std::ranges::all_of(white_balance, [](double value) {
        return std::isfinite(value) && value >= 0.0;
      })) {
    return {};
  }

  const auto lazy_alpha = std::make_shared<LazyCanonicalMaskAlpha>(
      LazyCanonicalMaskAlpha{
          .source_image = source_image,
          .compiled_masks = masks,
          .exposure_gain = exposure_gain,
          .white_balance = white_balance,
      });
  const auto expected_alpha_size =
      static_cast<std::size_t>(source_image.width()) *
      static_cast<std::size_t>(source_image.height());
  return evaluate_mask_alpha_cached(
      cache_key,
      source_image.size(),
      static_cast<int>(masks.size()),
      [lazy_alpha, expected_alpha_size](int x, int y, double, double) {
        lazy_alpha->initialize();
        if (lazy_alpha->alpha.size() != expected_alpha_size) {
          return 0.0;
        }
        const auto index = static_cast<std::size_t>(
            y * lazy_alpha->source_image.width() + x);
        return static_cast<double>(lazy_alpha->alpha[index]) / 255.0;
      });
}

QVariantMap canonical_composite_mask_thumbnail_alpha_metadata(
    const QImage& source_image,
    const QVariantList& masks,
    double exposure_gain,
    const std::array<double, 3>& white_balance) {
  const auto result = evaluate_canonical_composite_mask_alpha(
      source_image,
      masks,
      exposure_gain,
      white_balance);
  return result.valid ? mask_alpha_thumbnail_metadata(result) : QVariantMap{};
}

}  // namespace photo_editor::desktop
