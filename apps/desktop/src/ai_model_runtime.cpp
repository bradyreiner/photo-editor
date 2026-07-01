#include "ai_model_runtime.h"

#include <QColor>
#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRect>
#include <QSize>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#if defined(PHOTO_EDITOR_HAS_ONNXRUNTIME) && PHOTO_EDITOR_HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#if defined(PHOTO_EDITOR_ONNXRUNTIME_ENABLE_COREML) && PHOTO_EDITOR_ONNXRUNTIME_ENABLE_COREML
#include <coreml_provider_factory.h>
#endif
#endif

namespace photo_editor::desktop {
namespace {

#if defined(PHOTO_EDITOR_HAS_ONNXRUNTIME) && PHOTO_EDITOR_HAS_ONNXRUNTIME
constexpr bool kOnnxRuntimeLinked = true;
#else
constexpr bool kOnnxRuntimeLinked = false;
#endif
constexpr bool kSamRuntimeImplemented = false;
constexpr bool kLamaRuntimeImplemented = kOnnxRuntimeLinked;

QString default_model_root() {
  const auto env_root = qEnvironmentVariable("PHOTO_EDITOR_AI_MODEL_DIR");
  if (!env_root.trimmed().isEmpty()) {
    return env_root.trimmed();
  }

#if defined(Q_OS_MACOS)
  const QDir app_dir(QCoreApplication::applicationDirPath());
  const auto bundle_resources = app_dir.filePath(QStringLiteral("../Resources/models"));
  if (QFileInfo::exists(bundle_resources)) {
    return QFileInfo(bundle_resources).canonicalFilePath();
  }
#endif

  return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("models"));
}

bool model_file_exists(const QString& root, const QString& relative_path) {
  return QFileInfo::exists(QDir(root).filePath(relative_path));
}

QString inpaint_model_path(const QString& root) {
  return QDir(root).filePath(QStringLiteral("inpaint/lama.onnx"));
}

bool valid_inpaint_mask(const AiInpaintRequest& request) {
  if (request.source_image.isNull() || request.mask_width <= 0 ||
      request.mask_height <= 0 ||
      request.source_image.width() != request.mask_width ||
      request.source_image.height() != request.mask_height) {
    return false;
  }

  const auto expected_size =
      static_cast<qint64>(request.mask_width) * static_cast<qint64>(request.mask_height);
  return expected_size > 0 && request.mask_data.size() == expected_size;
}

QRect inpaint_mask_bounds(const AiInpaintRequest& request) {
  auto min_x = request.mask_width;
  auto min_y = request.mask_height;
  auto max_x = -1;
  auto max_y = -1;
  for (int y = 0; y < request.mask_height; ++y) {
    for (int x = 0; x < request.mask_width; ++x) {
      const auto value =
          static_cast<unsigned char>(request.mask_data.at(y * request.mask_width + x));
      if (value <= 0U) {
        continue;
      }
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
    }
  }

  if (max_x < min_x || max_y < min_y) {
    return {};
  }

  const auto padding = std::clamp(std::max(request.mask_width, request.mask_height) / 32, 8, 32);
  min_x = std::max(0, min_x - padding);
  min_y = std::max(0, min_y - padding);
  max_x = std::min(request.mask_width - 1, max_x + padding);
  max_y = std::min(request.mask_height - 1, max_y + padding);
  return QRect(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}

QColor average_patch_boundary_color(const QImage& source, const QRect& bounds) {
  qint64 red = 0;
  qint64 green = 0;
  qint64 blue = 0;
  qint64 alpha = 0;
  qint64 count = 0;
  const auto expanded = bounds.adjusted(-1, -1, 1, 1).intersected(source.rect());
  for (int y = expanded.top(); y <= expanded.bottom(); ++y) {
    for (int x = expanded.left(); x <= expanded.right(); ++x) {
      if (bounds.contains(x, y)) {
        continue;
      }
      const auto color = source.pixelColor(x, y);
      red += color.red();
      green += color.green();
      blue += color.blue();
      alpha += color.alpha();
      ++count;
    }
  }

  if (count <= 0) {
    const auto center = source.pixelColor(bounds.center());
    return center.isValid() ? center : QColor(Qt::transparent);
  }
  return QColor(
      static_cast<int>(red / count),
      static_cast<int>(green / count),
      static_cast<int>(blue / count),
      static_cast<int>(alpha / count));
}

QRect patch_source_rect(const AiInpaintRequest& request, const QSize& source_size) {
  if (!request.patch_source_set || !source_size.isValid()) {
    return {};
  }
  const auto width = std::clamp(
      static_cast<int>(std::lround(request.patch_source_width * source_size.width())),
      1,
      source_size.width());
  const auto height = std::clamp(
      static_cast<int>(std::lround(request.patch_source_height * source_size.height())),
      1,
      source_size.height());
  const auto x = std::clamp(
      static_cast<int>(std::lround(request.patch_source_x * source_size.width())),
      0,
      std::max(0, source_size.width() - width));
  const auto y = std::clamp(
      static_cast<int>(std::lround(request.patch_source_y * source_size.height())),
      0,
      std::max(0, source_size.height() - height));
  return QRect(x, y, width, height).intersected(QRect(QPoint(0, 0), source_size));
}

double inpaint_mask_coverage(const AiInpaintRequest& request, const QRect& bounds) {
  if (!bounds.isValid()) {
    return 0.0;
  }

  auto masked = 0;
  auto area = 0;
  for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
    for (int x = bounds.left(); x <= bounds.right(); ++x) {
      ++area;
      if (static_cast<unsigned char>(request.mask_data.at(y * request.mask_width + x)) > 0U) {
        ++masked;
      }
    }
  }
  return area > 0 ? static_cast<double>(masked) / static_cast<double>(area) : 0.0;
}

QString inpaint_region_kind(const AiInpaintRequest& request, double coverage) {
  const auto mode = request.retouch_mode.trimmed().toLower();
  if (mode == QStringLiteral("heal") && coverage <= 0.10) {
    return QStringLiteral("detail-heal");
  }
  if (mode == QStringLiteral("patch")) {
    return coverage >= 0.28 ? QStringLiteral("large-patch") : QStringLiteral("patch-region");
  }
  if (coverage <= 0.05) {
    return QStringLiteral("spot");
  }
  if (coverage >= 0.28) {
    return QStringLiteral("large-object");
  }
  return QStringLiteral("object");
}

double prompt_texture_multiplier(const QString& prompt) {
  const auto normalized = prompt.trimmed().toLower();
  if (normalized.isEmpty()) {
    return 1.0;
  }
  if (normalized.contains(QStringLiteral("smooth")) ||
      normalized.contains(QStringLiteral("soft")) ||
      normalized.contains(QStringLiteral("clean"))) {
    return 0.65;
  }
  if (normalized.contains(QStringLiteral("texture")) ||
      normalized.contains(QStringLiteral("grain")) ||
      normalized.contains(QStringLiteral("detail"))) {
    return 1.25;
  }
  return 1.0;
}

void populate_inpaint_region_result(
    AiInpaintResult* result,
    const AiInpaintRequest& request,
    const QRect& bounds) {
  if (result == nullptr || !bounds.isValid()) {
    return;
  }

  const auto coverage = inpaint_mask_coverage(request, bounds);
  result->prompt = request.prompt.trimmed();
  const auto quality_mode = request.quality_mode.trimmed().toLower();
  result->quality_mode =
      quality_mode == QStringLiteral("final") ? QStringLiteral("final") : QStringLiteral("preview");
  result->seed = std::max(1, request.seed);
  result->region_kind = inpaint_region_kind(request, coverage);
  result->region_coverage = coverage;
  result->region_x = bounds.x();
  result->region_y = bounds.y();
  result->region_width = bounds.width();
  result->region_height = bounds.height();
}

AiInpaintResult run_source_region_patch(const AiInpaintRequest& request) {
  AiInpaintResult result;
  if (!valid_inpaint_mask(request)) {
    result.error_message = QStringLiteral("Invalid inpaint request.");
    return result;
  }

  const auto bounds = inpaint_mask_bounds(request);
  if (!bounds.isValid()) {
    result.error_message = QStringLiteral("Inpaint mask is empty.");
    return result;
  }

  auto source = request.source_image;
  if (!source.colorSpace().isValid()) {
    source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  }
  if (source.format() != QImage::Format_ARGB32) {
    source = source.convertToFormat(QImage::Format_ARGB32);
  }
  const auto source_rect = patch_source_rect(request, source.size());
  if (!source_rect.isValid() || source_rect.isEmpty()) {
    result.error_message = QStringLiteral("Patch source region is empty.");
    return result;
  }

  auto patch = source.copy(source_rect).scaled(
      bounds.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  if (patch.format() != QImage::Format_ARGB32) {
    patch = patch.convertToFormat(QImage::Format_ARGB32);
  }
  patch.setColorSpace(source.colorSpace());

  result.success = true;
  result.model_name = QStringLiteral("source-region-patch-v1");
  populate_inpaint_region_result(&result, request, bounds);
  result.patch_image = patch;
  result.candidate_images = {patch};
  result.candidate_index = 0;
  result.patch_x = bounds.x();
  result.patch_y = bounds.y();
  result.patch_width = bounds.width();
  result.patch_height = bounds.height();
  return result;
}

AiInpaintResult run_cpu_diffusion_inpaint(const AiInpaintRequest& request, const QString& source_error = {}) {
  AiInpaintResult result;
  if (!valid_inpaint_mask(request)) {
    result.error_message = QStringLiteral("Invalid inpaint request.");
    return result;
  }

  const auto bounds = inpaint_mask_bounds(request);
  if (!bounds.isValid()) {
    result.error_message = QStringLiteral("Inpaint mask is empty.");
    return result;
  }
  populate_inpaint_region_result(&result, request, bounds);

  auto source = request.source_image;
  if (!source.colorSpace().isValid()) {
    source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  }
  if (source.format() != QImage::Format_ARGB32) {
    source = source.convertToFormat(QImage::Format_ARGB32);
  }

  auto patch = source.copy(bounds);
  const auto patch_width = patch.width();
  const auto patch_height = patch.height();
  std::vector<QRgb> pixels(static_cast<std::size_t>(patch_width * patch_height));
  std::vector<uint8_t> masked(static_cast<std::size_t>(patch_width * patch_height), 0U);
  std::vector<uint8_t> known(static_cast<std::size_t>(patch_width * patch_height), 0U);
  for (int y = 0; y < patch_height; ++y) {
    const auto* scanline = reinterpret_cast<const QRgb*>(patch.constScanLine(y));
    for (int x = 0; x < patch_width; ++x) {
      const auto index = static_cast<std::size_t>(y * patch_width + x);
      pixels[index] = scanline[x];
      const auto source_x = bounds.x() + x;
      const auto source_y = bounds.y() + y;
      const auto mask_value =
          static_cast<unsigned char>(request.mask_data.at(source_y * request.mask_width + source_x));
      masked[index] = mask_value > 0U ? 1U : 0U;
      known[index] = mask_value > 0U ? 0U : 1U;
    }
  }

  const auto fallback_color = average_patch_boundary_color(source, bounds);
  const auto fallback_rgb = fallback_color.rgba();
  const auto max_iterations = std::max(8, patch_width + patch_height);
  std::vector<QRgb> next_pixels = pixels;
  std::vector<uint8_t> next_known = known;
  for (int iteration = 0; iteration < max_iterations; ++iteration) {
    auto progressed = false;
    for (int y = 0; y < patch_height; ++y) {
      for (int x = 0; x < patch_width; ++x) {
        const auto index = static_cast<std::size_t>(y * patch_width + x);
        if (!masked[index] || known[index]) {
          continue;
        }

        double red = 0.0;
        double green = 0.0;
        double blue = 0.0;
        double alpha = 0.0;
        double weight = 0.0;
        for (int offset_y = -1; offset_y <= 1; ++offset_y) {
          const auto sample_y = y + offset_y;
          if (sample_y < 0 || sample_y >= patch_height) {
            continue;
          }
          for (int offset_x = -1; offset_x <= 1; ++offset_x) {
            const auto sample_x = x + offset_x;
            if ((offset_x == 0 && offset_y == 0) || sample_x < 0 || sample_x >= patch_width) {
              continue;
            }
            const auto sample_index = static_cast<std::size_t>(sample_y * patch_width + sample_x);
            if (!known[sample_index]) {
              continue;
            }
            const auto sample = pixels[sample_index];
            auto candidate_red = qRed(sample);
            auto candidate_green = qGreen(sample);
            auto candidate_blue = qBlue(sample);
            auto candidate_alpha = qAlpha(sample);
            auto candidate_weight = 1.0;
            const auto far_x = x + (offset_x * 2);
            const auto far_y = y + (offset_y * 2);
            if (far_x >= 0 && far_x < patch_width && far_y >= 0 && far_y < patch_height) {
              const auto far_index = static_cast<std::size_t>(far_y * patch_width + far_x);
              if (known[far_index]) {
                const auto far = pixels[far_index];
                candidate_red = std::clamp((qRed(sample) * 2) - qRed(far), 0, 255);
                candidate_green = std::clamp((qGreen(sample) * 2) - qGreen(far), 0, 255);
                candidate_blue = std::clamp((qBlue(sample) * 2) - qBlue(far), 0, 255);
                candidate_alpha = std::clamp((qAlpha(sample) * 2) - qAlpha(far), 0, 255);
                candidate_weight = 2.0;
              }
            }
            red += static_cast<double>(candidate_red) * candidate_weight;
            green += static_cast<double>(candidate_green) * candidate_weight;
            blue += static_cast<double>(candidate_blue) * candidate_weight;
            alpha += static_cast<double>(candidate_alpha) * candidate_weight;
            weight += candidate_weight;
          }
        }
        if (weight <= 0.0) {
          continue;
        }
        next_pixels[index] =
            qRgba(static_cast<int>(std::lround(red / weight)),
                  static_cast<int>(std::lround(green / weight)),
                  static_cast<int>(std::lround(blue / weight)),
                  static_cast<int>(std::lround(alpha / weight)));
        next_known[index] = 1U;
        progressed = true;
      }
    }
    pixels.swap(next_pixels);
    known.swap(next_known);
    if (!progressed) {
      break;
    }
  }

  for (std::size_t index = 0; index < pixels.size(); ++index) {
    if (masked[index] && !known[index]) {
      pixels[index] = fallback_rgb;
      known[index] = 1U;
    }
  }

  next_pixels = pixels;
  const auto smoothing_passes = result.region_kind == QStringLiteral("detail-heal") ||
                                      result.region_kind == QStringLiteral("spot")
                                  ? 1
                                  : result.region_kind == QStringLiteral("large-object") ||
                                        result.region_kind == QStringLiteral("large-patch")
                                      ? 3
                                      : 2;
  for (int pass = 0; pass < smoothing_passes; ++pass) {
    for (int y = 0; y < patch_height; ++y) {
      for (int x = 0; x < patch_width; ++x) {
        const auto index = static_cast<std::size_t>(y * patch_width + x);
        if (!masked[index]) {
          continue;
        }
        int red = qRed(pixels[index]) * 2;
        int green = qGreen(pixels[index]) * 2;
        int blue = qBlue(pixels[index]) * 2;
        int alpha = qAlpha(pixels[index]) * 2;
        int weight = 2;
        for (int offset_y = -1; offset_y <= 1; ++offset_y) {
          const auto sample_y = y + offset_y;
          if (sample_y < 0 || sample_y >= patch_height) {
            continue;
          }
          for (int offset_x = -1; offset_x <= 1; ++offset_x) {
            const auto sample_x = x + offset_x;
            if ((offset_x == 0 && offset_y == 0) || sample_x < 0 || sample_x >= patch_width) {
              continue;
            }
            const auto sample = pixels[static_cast<std::size_t>(sample_y * patch_width + sample_x)];
            red += qRed(sample);
            green += qGreen(sample);
            blue += qBlue(sample);
            alpha += qAlpha(sample);
            ++weight;
          }
        }
        next_pixels[index] =
            qRgba(red / weight, green / weight, blue / weight, alpha / weight);
      }
    }
    pixels.swap(next_pixels);
  }

  std::vector<std::array<int, 3>> texture_residuals;
  texture_residuals.reserve(static_cast<std::size_t>(patch_width + patch_height));
  for (int y = 0; y < patch_height; ++y) {
    for (int x = 0; x < patch_width; ++x) {
      const auto index = static_cast<std::size_t>(y * patch_width + x);
      if (masked[index]) {
        continue;
      }

      auto touches_mask = false;
      int red = 0;
      int green = 0;
      int blue = 0;
      int count = 0;
      for (int offset_y = -1; offset_y <= 1; ++offset_y) {
        const auto sample_y = y + offset_y;
        if (sample_y < 0 || sample_y >= patch_height) {
          continue;
        }
        for (int offset_x = -1; offset_x <= 1; ++offset_x) {
          const auto sample_x = x + offset_x;
          if ((offset_x == 0 && offset_y == 0) || sample_x < 0 || sample_x >= patch_width) {
            continue;
          }
          const auto sample_index = static_cast<std::size_t>(sample_y * patch_width + sample_x);
          if (masked[sample_index]) {
            touches_mask = true;
            continue;
          }
          const auto sample = pixels[sample_index];
          red += qRed(sample);
          green += qGreen(sample);
          blue += qBlue(sample);
          ++count;
        }
      }
      if (!touches_mask || count <= 0) {
        continue;
      }

      const auto pixel = pixels[index];
      const std::array<int, 3> residual{
          qRed(pixel) - (red / count),
          qGreen(pixel) - (green / count),
          qBlue(pixel) - (blue / count),
      };
      if (std::abs(residual[0]) + std::abs(residual[1]) + std::abs(residual[2]) > 2) {
        texture_residuals.push_back(residual);
      }
    }
  }

  if (!texture_residuals.empty()) {
    const auto base_texture_strength = result.region_kind == QStringLiteral("detail-heal") ||
                                               result.region_kind == QStringLiteral("spot")
                                           ? 0.22
                                           : result.region_kind == QStringLiteral("large-object") ||
                                                 result.region_kind == QStringLiteral("large-patch")
                                               ? 0.34
                                               : 0.28;
    const auto heal_texture_multiplier =
        request.retouch_mode.trimmed().toLower() == QStringLiteral("heal")
            ? std::clamp(request.heal_texture_blend, 0.0, 1.0)
            : 1.0;
    const auto texture_strength =
        std::clamp(base_texture_strength * prompt_texture_multiplier(result.prompt) *
                       heal_texture_multiplier,
                   0.0,
                   0.5);
    for (int y = 0; y < patch_height; ++y) {
      for (int x = 0; x < patch_width; ++x) {
        const auto index = static_cast<std::size_t>(y * patch_width + x);
        if (!masked[index]) {
          continue;
        }
        const auto residual_index =
            (static_cast<std::size_t>(x) * 73856093ULL +
             static_cast<std::size_t>(y) * 19349663ULL +
             static_cast<std::size_t>(result.seed) * 83492791ULL) %
            texture_residuals.size();
        const auto residual = texture_residuals[residual_index];
        const auto pixel = pixels[index];
        pixels[index] = qRgba(
            std::clamp(qRed(pixel) + static_cast<int>(std::lround(residual[0] * texture_strength)), 0, 255),
            std::clamp(qGreen(pixel) + static_cast<int>(std::lround(residual[1] * texture_strength)), 0, 255),
            std::clamp(qBlue(pixel) + static_cast<int>(std::lround(residual[2] * texture_strength)), 0, 255),
            qAlpha(pixel));
      }
    }
    result.texture_synthesis = true;
    result.texture_strength = texture_strength;
  }

  for (int y = 0; y < patch_height; ++y) {
    auto* scanline = reinterpret_cast<QRgb*>(patch.scanLine(y));
    for (int x = 0; x < patch_width; ++x) {
      scanline[x] = pixels[static_cast<std::size_t>(y * patch_width + x)];
    }
  }
  patch.setColorSpace(source.colorSpace());

  const auto image_from_pixels = [&](const std::vector<QRgb>& candidate_pixels) {
    auto image = patch;
    for (int y = 0; y < patch_height; ++y) {
      auto* scanline = reinterpret_cast<QRgb*>(image.scanLine(y));
      for (int x = 0; x < patch_width; ++x) {
        scanline[x] = candidate_pixels[static_cast<std::size_t>(y * patch_width + x)];
      }
    }
    image.setColorSpace(source.colorSpace());
    return image;
  };

  auto smooth_candidate = pixels;
  for (int y = 0; y < patch_height; ++y) {
    for (int x = 0; x < patch_width; ++x) {
      const auto index = static_cast<std::size_t>(y * patch_width + x);
      if (!masked[index]) {
        continue;
      }
      int red = 0;
      int green = 0;
      int blue = 0;
      int alpha = 0;
      int count = 0;
      for (int offset_y = -1; offset_y <= 1; ++offset_y) {
        const auto sample_y = y + offset_y;
        if (sample_y < 0 || sample_y >= patch_height) {
          continue;
        }
        for (int offset_x = -1; offset_x <= 1; ++offset_x) {
          const auto sample_x = x + offset_x;
          if (sample_x < 0 || sample_x >= patch_width) {
            continue;
          }
          const auto sample = pixels[static_cast<std::size_t>(sample_y * patch_width + sample_x)];
          red += qRed(sample);
          green += qGreen(sample);
          blue += qBlue(sample);
          alpha += qAlpha(sample);
          ++count;
        }
      }
      if (count > 0) {
        smooth_candidate[index] = qRgba(red / count, green / count, blue / count, alpha / count);
      }
    }
  }

  auto textured_candidate = pixels;
  if (!texture_residuals.empty()) {
    const auto boost = std::clamp(result.texture_strength * 1.35, 0.0, 0.65);
    for (int y = 0; y < patch_height; ++y) {
      for (int x = 0; x < patch_width; ++x) {
        const auto index = static_cast<std::size_t>(y * patch_width + x);
        if (!masked[index]) {
          continue;
        }
        const auto residual_index =
            (static_cast<std::size_t>(x) * 83492791ULL +
             static_cast<std::size_t>(y) * 2971215073ULL +
             static_cast<std::size_t>(result.seed) * 73856093ULL) %
            texture_residuals.size();
        const auto residual = texture_residuals[residual_index];
        const auto pixel = pixels[index];
        textured_candidate[index] = qRgba(
            std::clamp(qRed(pixel) + static_cast<int>(std::lround(residual[0] * boost)), 0, 255),
            std::clamp(qGreen(pixel) + static_cast<int>(std::lround(residual[1] * boost)), 0, 255),
            std::clamp(qBlue(pixel) + static_cast<int>(std::lround(residual[2] * boost)), 0, 255),
            qAlpha(pixel));
      }
    }
  }

  result.success = true;
  result.fallback = true;
  result.edge_continuation = true;
  result.model_name = QStringLiteral("cpu-diffusion-inpaint-v1");
  result.error_message = source_error;
  result.patch_image = patch;
  result.candidate_images = {patch, image_from_pixels(smooth_candidate), image_from_pixels(textured_candidate)};
  result.candidate_index = 0;
  result.patch_x = bounds.x();
  result.patch_y = bounds.y();
  result.patch_width = bounds.width();
  result.patch_height = bounds.height();
  return result;
}

AiInpaintResult run_fake_inpaint(const AiInpaintRequest& request) {
  AiInpaintResult result;
  if (!valid_inpaint_mask(request)) {
    result.error_message = QStringLiteral("Invalid inpaint request.");
    return result;
  }

  const auto bounds = inpaint_mask_bounds(request);
  if (!bounds.isValid()) {
    result.error_message = QStringLiteral("Inpaint mask is empty.");
    return result;
  }

  auto source = request.source_image;
  if (source.format() != QImage::Format_ARGB32) {
    source = source.convertToFormat(QImage::Format_ARGB32);
  }

  auto patch = source.copy(bounds);
  const auto fill_color = average_patch_boundary_color(source, bounds);
  for (int y = 0; y < patch.height(); ++y) {
    auto* scanline = reinterpret_cast<QRgb*>(patch.scanLine(y));
    const auto source_y = bounds.y() + y;
    for (int x = 0; x < patch.width(); ++x) {
      const auto source_x = bounds.x() + x;
      const auto mask_value =
          static_cast<unsigned char>(request.mask_data.at(source_y * request.mask_width + source_x));
      if (mask_value <= 0U) {
        continue;
      }
      scanline[x] = fill_color.rgba();
    }
  }

  result.success = true;
  result.model_name = QStringLiteral("test-fake-lama");
  populate_inpaint_region_result(&result, request, bounds);
  result.patch_image = patch;
  result.candidate_images = {patch};
  result.patch_x = bounds.x();
  result.patch_y = bounds.y();
  result.patch_width = bounds.width();
  result.patch_height = bounds.height();
  return result;
}

#if defined(PHOTO_EDITOR_HAS_ONNXRUNTIME) && PHOTO_EDITOR_HAS_ONNXRUNTIME
QByteArray mask_patch_bytes(const AiInpaintRequest& request, const QRect& bounds) {
  QByteArray patch(bounds.width() * bounds.height(), '\0');
  for (int y = 0; y < bounds.height(); ++y) {
    const auto source_y = bounds.y() + y;
    for (int x = 0; x < bounds.width(); ++x) {
      const auto source_x = bounds.x() + x;
      patch[y * bounds.width() + x] =
          request.mask_data.at(source_y * request.mask_width + source_x);
    }
  }
  return patch;
}

QSize model_input_size_for_patch(const QSize& patch_size) {
  if (!patch_size.isValid()) {
    return {};
  }

  const auto round_up_to_multiple = [](int value, int multiple) {
    return std::max(multiple, ((value + multiple - 1) / multiple) * multiple);
  };
  return {
      round_up_to_multiple(patch_size.width(), 8),
      round_up_to_multiple(patch_size.height(), 8),
  };
}

std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

struct NamedOrtValue final {
  Ort::AllocatedStringPtr allocated_name;
  std::string fallback_name;

  [[nodiscard]] const char* name() const {
    return allocated_name ? allocated_name.get() : fallback_name.c_str();
  }
};

NamedOrtValue ort_value_name(
    Ort::Session* session,
    std::size_t index,
    bool input,
    const char* fallback) {
  Ort::AllocatorWithDefaultOptions allocator;
  try {
    return NamedOrtValue{
        .allocated_name = input ? session->GetInputNameAllocated(index, allocator)
                                : session->GetOutputNameAllocated(index, allocator),
        .fallback_name = fallback,
    };
  } catch (const Ort::Exception&) {
    return NamedOrtValue{
        .allocated_name = nullptr,
        .fallback_name = fallback,
    };
  }
}

class OnnxAiRuntime final {
 public:
  [[nodiscard]] AiInpaintResult runInpaint(const AiInpaintRequest& request) {
    AiInpaintResult result;
    if (!valid_inpaint_mask(request)) {
      result.error_message = QStringLiteral("Invalid inpaint request.");
      return result;
    }

    const auto bounds = inpaint_mask_bounds(request);
    if (!bounds.isValid()) {
      result.error_message = QStringLiteral("Inpaint mask is empty.");
      return result;
    }

    const auto model_path = inpaint_model_path(default_model_root());
    const QFileInfo model_info(model_path);
    if (!model_info.exists()) {
      result.error_message = QStringLiteral("LaMa inpaint model is missing at %1.").arg(model_path);
      return result;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureInpaintSession(model_info.absoluteFilePath(), &result.error_message)) {
      return result;
    }

    return runInpaintSession(request, bounds);
  }

 private:
  [[nodiscard]] bool ensureInpaintSession(const QString& model_path, QString* error_message) {
    if (inpaint_session_ && inpaint_model_path_ == model_path) {
      return true;
    }

    try {
      if (!env_) {
        env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "photo_editor_ai");
      }

      Ort::SessionOptions options;
      options.SetIntraOpNumThreads(1);
      options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#if defined(PHOTO_EDITOR_ONNXRUNTIME_ENABLE_COREML) && PHOTO_EDITOR_ONNXRUNTIME_ENABLE_COREML
      OrtSessionOptionsAppendExecutionProvider_CoreML(options, 0);
#endif
      inpaint_session_ = std::make_unique<Ort::Session>(
          *env_,
          model_path.toUtf8().constData(),
          options);
      inpaint_model_path_ = model_path;
      return true;
    } catch (const Ort::Exception& exception) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Unable to load LaMa ONNX session: %1")
                             .arg(QString::fromUtf8(exception.what()));
      }
    } catch (const std::exception& exception) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Unable to load LaMa ONNX session: %1")
                             .arg(QString::fromUtf8(exception.what()));
      }
    }

    inpaint_session_.reset();
    inpaint_model_path_.clear();
    return false;
  }

  [[nodiscard]] AiInpaintResult runInpaintSession(
      const AiInpaintRequest& request,
      const QRect& bounds) {
    AiInpaintResult result;
    if (!inpaint_session_) {
      result.error_message = QStringLiteral("LaMa inpaint session is not loaded.");
      return result;
    }

    const auto input_count = inpaint_session_->GetInputCount();
    const auto output_count = inpaint_session_->GetOutputCount();
    if (input_count < 2 || output_count < 1) {
      result.error_message =
          QStringLiteral("LaMa ONNX model must expose at least two inputs and one output.");
      return result;
    }

    try {
      auto source = request.source_image;
      if (!source.colorSpace().isValid()) {
        source.setColorSpace(QColorSpace(QColorSpace::SRgb));
      }
      if (source.format() != QImage::Format_ARGB32) {
        source = source.convertToFormat(QImage::Format_ARGB32);
      }
      const auto patch_source = source.copy(bounds);
      const auto patch_mask = mask_patch_bytes(request, bounds);
      const auto model_size = model_input_size_for_patch(bounds.size());
      if (!model_size.isValid()) {
        result.error_message = QStringLiteral("Invalid LaMa inpaint patch bounds.");
        return result;
      }

      const auto model_width = model_size.width();
      const auto model_height = model_size.height();
      const auto scaled_patch =
          patch_source.scaled(model_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
              .convertToFormat(QImage::Format_ARGB32);
      std::vector<float> image_tensor(static_cast<std::size_t>(3 * model_width * model_height), 0.0F);
      std::vector<float> mask_tensor(static_cast<std::size_t>(model_width * model_height), 0.0F);
      const auto plane_size = static_cast<std::size_t>(model_width * model_height);
      for (int y = 0; y < model_height; ++y) {
        const auto* scanline = reinterpret_cast<const QRgb*>(scaled_patch.constScanLine(y));
        const auto source_y =
            std::clamp(static_cast<int>((static_cast<double>(y) / std::max(1, model_height)) *
                                        static_cast<double>(bounds.height())),
                       0,
                       std::max(0, bounds.height() - 1));
        for (int x = 0; x < model_width; ++x) {
          const auto source_x =
              std::clamp(static_cast<int>((static_cast<double>(x) / std::max(1, model_width)) *
                                          static_cast<double>(bounds.width())),
                         0,
                         std::max(0, bounds.width() - 1));
          const auto tensor_index = static_cast<std::size_t>(y * model_width + x);
          const auto pixel = scanline[x];
          image_tensor[tensor_index] = static_cast<float>(qRed(pixel)) / 255.0F;
          image_tensor[plane_size + tensor_index] = static_cast<float>(qGreen(pixel)) / 255.0F;
          image_tensor[(plane_size * 2) + tensor_index] = static_cast<float>(qBlue(pixel)) / 255.0F;
          mask_tensor[tensor_index] =
              static_cast<unsigned char>(patch_mask.at(source_y * bounds.width() + source_x)) > 0U
                  ? 1.0F
                  : 0.0F;
        }
      }

      std::array<int64_t, 4> image_shape{
          1,
          3,
          static_cast<int64_t>(model_height),
          static_cast<int64_t>(model_width)};
      std::array<int64_t, 4> mask_shape{
          1,
          1,
          static_cast<int64_t>(model_height),
          static_cast<int64_t>(model_width)};
      auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
      std::vector<Ort::Value> input_tensors;
      input_tensors.emplace_back(Ort::Value::CreateTensor<float>(
          memory_info,
          image_tensor.data(),
          image_tensor.size(),
          image_shape.data(),
          image_shape.size()));
      input_tensors.emplace_back(Ort::Value::CreateTensor<float>(
          memory_info,
          mask_tensor.data(),
          mask_tensor.size(),
          mask_shape.data(),
          mask_shape.size()));

      std::vector<NamedOrtValue> input_names;
      input_names.reserve(2);
      input_names.push_back(ort_value_name(inpaint_session_.get(), 0, true, "image"));
      input_names.push_back(ort_value_name(inpaint_session_.get(), 1, true, "mask"));
      if (lower_ascii(input_names.at(0).name()).find("mask") != std::string::npos &&
          lower_ascii(input_names.at(1).name()).find("mask") == std::string::npos) {
        std::swap(input_names[0], input_names[1]);
        std::swap(input_tensors[0], input_tensors[1]);
      }
      std::vector<const char*> input_name_ptrs;
      input_name_ptrs.reserve(input_names.size());
      for (const auto& input_name : input_names) {
        input_name_ptrs.push_back(input_name.name());
      }

      auto output_name = ort_value_name(inpaint_session_.get(), 0, false, "output");
      std::array<const char*, 1> output_name_ptrs{output_name.name()};
      auto outputs = inpaint_session_->Run(
          Ort::RunOptions{nullptr},
          input_name_ptrs.data(),
          input_tensors.data(),
          input_tensors.size(),
          output_name_ptrs.data(),
          output_name_ptrs.size());
      if (outputs.empty() || !outputs.front().IsTensor()) {
        result.error_message = QStringLiteral("LaMa ONNX model did not return an image tensor.");
        return result;
      }

      auto& output = outputs.front();
      const auto output_info = output.GetTensorTypeAndShapeInfo();
      const auto output_shape = output_info.GetShape();
      const auto* output_data = output.GetTensorData<float>();
      if (output_data == nullptr || output_shape.size() != 4) {
        result.error_message =
            QStringLiteral("LaMa ONNX output tensor must be 4D float image data.");
        return result;
      }

      const auto nchw = output_shape[1] == 3;
      const auto nhwc = output_shape[3] == 3;
      if (!nchw && !nhwc) {
        result.error_message =
            QStringLiteral("LaMa ONNX output tensor must have three color channels.");
        return result;
      }
      const auto output_height = static_cast<int>(nchw ? output_shape[2] : output_shape[1]);
      const auto output_width = static_cast<int>(nchw ? output_shape[3] : output_shape[2]);
      if (output_width <= 0 || output_height <= 0) {
        result.error_message = QStringLiteral("LaMa ONNX output image is empty.");
        return result;
      }

      QImage output_image(output_width, output_height, QImage::Format_ARGB32);
      const auto output_plane_size = static_cast<std::size_t>(output_width * output_height);
      auto output_max = 0.0F;
      for (std::size_t index = 0; index < output_info.GetElementCount(); ++index) {
        output_max = std::max(output_max, output_data[index]);
      }
      const auto output_scale = output_max > 2.0F ? 255.0F : 1.0F;
      const auto sample_output = [&](int x, int y, int channel) {
        const auto value =
            nchw
                ? output_data[(static_cast<std::size_t>(channel) * output_plane_size) +
                              static_cast<std::size_t>(y * output_width + x)]
                : output_data[(static_cast<std::size_t>(y * output_width + x) * 3U) +
                              static_cast<std::size_t>(channel)];
        return std::clamp(static_cast<int>(std::lround((value / output_scale) * 255.0F)), 0, 255);
      };
      for (int y = 0; y < output_height; ++y) {
        auto* scanline = reinterpret_cast<QRgb*>(output_image.scanLine(y));
        for (int x = 0; x < output_width; ++x) {
          scanline[x] = qRgb(sample_output(x, y, 0), sample_output(x, y, 1), sample_output(x, y, 2));
        }
      }

      if (output_image.size() != bounds.size()) {
        output_image =
            output_image.scaled(bounds.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
      }
      output_image.setColorSpace(QColorSpace(QColorSpace::SRgb));
      result.success = true;
      result.model_name = QStringLiteral("lama-onnx");
      populate_inpaint_region_result(&result, request, bounds);
      result.patch_image = output_image;
      result.candidate_images = {output_image};
      result.patch_x = bounds.x();
      result.patch_y = bounds.y();
      result.patch_width = bounds.width();
      result.patch_height = bounds.height();
      return result;
    } catch (const Ort::Exception& exception) {
      result.error_message = QStringLiteral("LaMa ONNX inference failed: %1")
                                 .arg(QString::fromUtf8(exception.what()));
    } catch (const std::exception& exception) {
      result.error_message = QStringLiteral("LaMa ONNX inference failed: %1")
                                 .arg(QString::fromUtf8(exception.what()));
    }
    return result;
  }

  std::mutex mutex_;
  std::unique_ptr<Ort::Env> env_;
  std::unique_ptr<Ort::Session> inpaint_session_;
  QString inpaint_model_path_;
};

OnnxAiRuntime& onnx_ai_runtime() {
  static OnnxAiRuntime runtime;
  return runtime;
}
#endif

}  // namespace

AiModelRuntimeStatus ai_model_runtime_status() {
  AiModelRuntimeStatus status;
  status.model_root = default_model_root();

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_FAKE_AI_MODELS")) {
    status.smart_selection_available = true;
    status.inpaint_available = true;
    status.smart_selection_runtime_label = QStringLiteral("SAM Smart Select");
    status.inpaint_runtime_label = QStringLiteral("LaMa Model Inpaint");
    status.diagnostics = QStringLiteral(
        "root:%1|manifest:test|sam:test-ready|lama:test-ready|onnx:%2")
                             .arg(
                                 status.model_root,
                                 kOnnxRuntimeLinked ? QStringLiteral("linked")
                                                    : QStringLiteral("not-linked"));
    status.summary = QStringLiteral("Test SAM Smart Select and LaMa Model Inpaint runtimes active.");
    return status;
  }

  const auto manifest_available =
      model_file_exists(status.model_root, QStringLiteral("manifest.json"));
  const auto smart_selection_files_available =
      manifest_available &&
      model_file_exists(
          status.model_root,
          QStringLiteral("selection/sam2.1_hiera_tiny.onnx"));
  const auto inpaint_files_available =
      manifest_available &&
      QFileInfo::exists(inpaint_model_path(status.model_root));
  status.smart_selection_available =
      kSamRuntimeImplemented && smart_selection_files_available;
  status.inpaint_available =
      kLamaRuntimeImplemented && inpaint_files_available;
  status.smart_selection_runtime_label =
      status.smart_selection_available ? QStringLiteral("SAM Smart Select")
                                       : QStringLiteral("Local Smart Select");
  status.inpaint_runtime_label =
      status.inpaint_available ? QStringLiteral("LaMa Model Inpaint")
                               : QStringLiteral("Local CPU Fill");
  const auto sam_state =
      status.smart_selection_available
          ? QStringLiteral("ready")
          : (!smart_selection_files_available
                 ? QStringLiteral("model-missing")
                 : QStringLiteral("runtime-missing"));
  const auto lama_state =
      status.inpaint_available
          ? QStringLiteral("ready")
          : (!inpaint_files_available ? QStringLiteral("model-missing")
                                      : QStringLiteral("runtime-missing"));
  status.diagnostics =
      QStringLiteral("root:%1|manifest:%2|sam:%3|lama:%4|onnx:%5")
          .arg(
              status.model_root,
              manifest_available ? QStringLiteral("present") : QStringLiteral("missing"),
              sam_state,
              lama_state,
              kOnnxRuntimeLinked ? QStringLiteral("linked") : QStringLiteral("not-linked"));

  if (status.smart_selection_available && status.inpaint_available) {
    status.summary = QStringLiteral("Bundled SAM Smart Select and LaMa Model Inpaint runtimes ready.");
  } else if (inpaint_files_available && !kLamaRuntimeImplemented) {
    status.summary = QStringLiteral("Bundled LaMa inpaint model found, but ONNX Runtime is not linked in this build; retouch uses Local CPU Fill.");
  } else if (smart_selection_files_available && !kSamRuntimeImplemented) {
    status.summary = QStringLiteral("Bundled smart-selection model found, but SAM ONNX inference is not implemented in this build.");
  } else if (manifest_available) {
    status.summary = QStringLiteral("Bundled model manifest found, but required SAM/LaMa model files are incomplete; retouch uses Local CPU Fill.");
  } else {
    status.summary = QStringLiteral("Bundled SAM/LaMa models missing; retouch uses Local CPU Fill.");
  }
  return status;
}

SegmentationResult generate_ai_smart_selection_mask(
    const QImage& source_image,
    const QVector<SmartSelectPrompt>& prompts) {
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_FAKE_AI_MODELS")) {
    return generate_smart_select_mask(source_image, prompts);
  }

  SegmentationResult result;
  result.error_message =
      QStringLiteral("SAM smart-selection runtime is not linked in this build.");
  return result;
}

AiInpaintResult run_ai_inpaint(const AiInpaintRequest& request) {
  if (request.retouch_mode.trimmed().toLower() == QStringLiteral("patch") &&
      request.patch_source_set) {
    return run_source_region_patch(request);
  }

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_FAKE_AI_MODELS")) {
    return run_fake_inpaint(request);
  }

  AiInpaintResult result;
  if (!valid_inpaint_mask(request)) {
    result.error_message = QStringLiteral("Invalid inpaint request.");
    return result;
  }

#if defined(PHOTO_EDITOR_HAS_ONNXRUNTIME) && PHOTO_EDITOR_HAS_ONNXRUNTIME
  result = onnx_ai_runtime().runInpaint(request);
  if (result.success) {
    return result;
  }
  return run_cpu_diffusion_inpaint(request, result.error_message);
#else
  result.error_message =
      QStringLiteral("LaMa inpaint runtime is not linked in this build.");
  return run_cpu_diffusion_inpaint(request, result.error_message);
#endif
}

}  // namespace photo_editor::desktop
