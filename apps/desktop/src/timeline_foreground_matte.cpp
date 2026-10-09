#include "timeline_foreground_matte.h"

#include <QColorSpace>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>

#include <algorithm>
#include <cmath>
#include <limits>

namespace photo_editor::desktop {
namespace {

QString matte_error(const QString& reason) {
  return QStringLiteral("Foreground cutout is enabled, but its matte is %1.")
      .arg(reason);
}

bool valid_metadata(
    const QVariantMap& metadata,
    const QString& source_signature) {
  const auto directory = metadata.value(QStringLiteral("directory")).toString().trimmed();
  const auto fps = metadata.value(QStringLiteral("fps")).toDouble();
  const auto trim_start = metadata.value(QStringLiteral("trimStart")).toDouble();
  const auto trim_end = metadata.value(QStringLiteral("trimEnd")).toDouble();
  const auto frame_count = metadata.value(QStringLiteral("frameCount")).toInt();
  const auto expected_signature =
      metadata.value(QStringLiteral("sourceSignature")).toString().trimmed();
  const QFileInfo directory_info(directory);
  constexpr auto kMaximumAnalysisFps = 60.0;
  constexpr auto kMaximumAnalysisFrames = 900;
  constexpr auto kMinimumAnalysisFrames = 2;
  constexpr auto kFrameCountEpsilon = 1e-7;
  const auto analyzed_frame_span = (trim_end - trim_start) * fps;
  const auto expected_frame_count = std::isfinite(analyzed_frame_span)
      ? std::ceil(analyzed_frame_span - kFrameCountEpsilon)
      : std::numeric_limits<double>::infinity();
  return !directory.isEmpty() && directory_info.exists() && directory_info.isDir() &&
      directory_info.isReadable() && std::isfinite(fps) && fps > 0.0 &&
      fps <= kMaximumAnalysisFps &&
      std::isfinite(trim_start) && std::isfinite(trim_end) &&
      trim_start >= 0.0 && trim_end > trim_start &&
      std::isfinite(analyzed_frame_span) &&
      frame_count >= kMinimumAnalysisFrames &&
      frame_count <= kMaximumAnalysisFrames &&
      expected_frame_count == static_cast<double>(frame_count) &&
      !source_signature.trimmed().isEmpty() &&
      expected_signature == source_signature.trimmed();
}

}  // namespace

bool timelineForegroundCutoutReady(
    const QVariantMap& metadata,
    const QString& source_signature,
    double clip_trim_start_seconds,
    double clip_trim_end_seconds) {
  if (!valid_metadata(metadata, source_signature) ||
      !std::isfinite(clip_trim_start_seconds) ||
      !std::isfinite(clip_trim_end_seconds) ||
      clip_trim_end_seconds <= clip_trim_start_seconds) {
    return false;
  }
  const auto analyzed_start = metadata.value(QStringLiteral("trimStart")).toDouble();
  const auto analyzed_end = metadata.value(QStringLiteral("trimEnd")).toDouble();
  constexpr auto kTimeEpsilon = 1e-6;
  return clip_trim_start_seconds >= analyzed_start - kTimeEpsilon &&
      clip_trim_end_seconds <= analyzed_end + kTimeEpsilon;
}

QString timelineForegroundMaskPath(
    const QVariantMap& metadata,
    const QString& source_signature,
    double source_seconds,
    QString* error_message) {
  if (!valid_metadata(metadata, source_signature)) {
    if (error_message != nullptr) {
      *error_message = matte_error(QStringLiteral("stale or incomplete"));
    }
    return {};
  }

  const auto fps = metadata.value(QStringLiteral("fps")).toDouble();
  const auto trim_start = metadata.value(QStringLiteral("trimStart")).toDouble();
  const auto trim_end = metadata.value(QStringLiteral("trimEnd")).toDouble();
  const auto frame_count = metadata.value(QStringLiteral("frameCount")).toInt();
  if (!std::isfinite(source_seconds) || source_seconds < trim_start ||
      source_seconds >= trim_end) {
    if (error_message != nullptr) {
      *error_message = matte_error(QStringLiteral("outside its analyzed time range"));
    }
    return {};
  }

  const auto raw_index = std::floor((source_seconds - trim_start) * fps);
  if (!std::isfinite(raw_index) || raw_index < 0.0 ||
      raw_index >= static_cast<double>(frame_count)) {
    if (error_message != nullptr) {
      *error_message = matte_error(QStringLiteral("missing coverage for this frame"));
    }
    return {};
  }
  const auto frame_index = std::clamp(static_cast<int>(raw_index), 0, frame_count - 1);
  const auto path = QDir(metadata.value(QStringLiteral("directory")).toString()).filePath(
      QStringLiteral("mask_%1.png").arg(frame_index, 6, 10, QLatin1Char('0')));
  const QFileInfo mask_info(path);
  if (!mask_info.exists() || !mask_info.isFile() || !mask_info.isReadable()) {
    if (error_message != nullptr) {
      *error_message = matte_error(QStringLiteral("missing a frame mask"));
    }
    return {};
  }
  return path;
}

bool applyTimelineForegroundMask(
    QImage* source,
    const QString& mask_path,
    QString* error_message) {
  if (source == nullptr || source->isNull() || mask_path.trimmed().isEmpty()) {
    if (error_message != nullptr) {
      *error_message = matte_error(QStringLiteral("unavailable"));
    }
    return false;
  }

  QImageReader reader(mask_path);
  reader.setAutoTransform(false);
  const auto loaded_mask = reader.read();
  if (loaded_mask.isNull()) {
    if (error_message != nullptr) {
      *error_message = matte_error(QStringLiteral("unreadable"));
    }
    return false;
  }
  auto mask = loaded_mask.convertToFormat(QImage::Format_ARGB32);
  if (mask.isNull()) {
    if (error_message != nullptr) {
      *error_message = matte_error(QStringLiteral("invalid"));
    }
    return false;
  }
  auto alpha_mask = QImage(mask.size(), QImage::Format_Grayscale8);
  if (alpha_mask.isNull()) {
    if (error_message != nullptr) {
      *error_message = matte_error(QStringLiteral("could not be decoded"));
    }
    return false;
  }
  for (int y = 0; y < mask.height(); ++y) {
    const auto* mask_row = reinterpret_cast<const QRgb*>(mask.constScanLine(y));
    auto* alpha_row = alpha_mask.scanLine(y);
    for (int x = 0; x < mask.width(); ++x) {
      alpha_row[x] = static_cast<unsigned char>(qAlpha(mask_row[x]));
    }
  }
  if (alpha_mask.size() != source->size()) {
    alpha_mask = alpha_mask.scaled(
        source->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  }
  if (alpha_mask.isNull()) {
    if (error_message != nullptr) {
      *error_message = matte_error(QStringLiteral("could not be resized"));
    }
    return false;
  }

  auto output = source->convertToFormat(QImage::Format_ARGB32_Premultiplied);
  if (output.isNull()) {
    if (error_message != nullptr) {
      *error_message = matte_error(QStringLiteral("could not be applied"));
    }
    return false;
  }
  output.setColorSpace(source->colorSpace());
  for (int y = 0; y < output.height(); ++y) {
    auto* output_row = reinterpret_cast<QRgb*>(output.scanLine(y));
    const auto* matte_row = alpha_mask.constScanLine(y);
    for (int x = 0; x < output.width(); ++x) {
      const auto matte_alpha = static_cast<unsigned char>(matte_row[x]);
      const auto pixel = output_row[x];
      const auto multiply = [matte_alpha](int channel) {
        return (channel * static_cast<int>(matte_alpha) + 127) / 255;
      };
      output_row[x] = qRgba(
          multiply(qRed(pixel)), multiply(qGreen(pixel)), multiply(qBlue(pixel)),
          multiply(qAlpha(pixel)));
    }
  }
  *source = std::move(output);
  return true;
}

TimelineForegroundMatteState evaluate_timeline_foreground_matte(
    const QVariantMap& clip) {
  TimelineForegroundMatteState state;
  const auto metadata = clip.value(QStringLiteral("foregroundCutout")).toMap();
  state.enabled = clip.contains(QStringLiteral("foregroundCutoutEnabled"))
                      ? clip.value(QStringLiteral("foregroundCutoutEnabled")).toBool()
                      : metadata.value(QStringLiteral("enabled")).toBool();
  if (!state.enabled) {
    state.ready = true;
    return state;
  }
  state.directory = metadata.value(QStringLiteral("directory")).toString().trimmed();
  state.fps = metadata.value(QStringLiteral("fps")).toDouble();
  state.trim_start_seconds = metadata.value(QStringLiteral("trimStart")).toDouble();
  state.trim_end_seconds = metadata.value(QStringLiteral("trimEnd")).toDouble();
  state.frame_count = metadata.value(QStringLiteral("frameCount")).toInt();
  const auto signature = clip.value(QStringLiteral("sourceIdentitySignature"))
                             .toString().trimmed();
  state.source_signature = signature;
  const auto clip_trim_start = clip.value(QStringLiteral("trimStartSeconds"), state.trim_start_seconds)
                                   .toDouble();
  const auto clip_trim_end = clip.value(QStringLiteral("trimEndSeconds"), state.trim_end_seconds)
                                 .toDouble();
  state.ready = timelineForegroundCutoutReady(
      metadata, signature, clip_trim_start, clip_trim_end);
  if (clip.contains(QStringLiteral("foregroundCutoutReady")) &&
      !clip.value(QStringLiteral("foregroundCutoutReady")).toBool()) {
    state.ready = false;
  }
  if (!state.ready) {
    state.error_message = matte_error(QStringLiteral("stale or incomplete"));
  }
  return state;
}

QString timeline_foreground_matte_frame_path(
    const TimelineForegroundMatteState& state,
    double source_seconds,
    QString* error_message) {
  if (!state.enabled) {
    return {};
  }
  if (!state.ready) {
    if (error_message != nullptr) {
      *error_message = state.error_message.isEmpty()
                           ? matte_error(QStringLiteral("unavailable"))
                           : state.error_message;
    }
    return {};
  }
  const QVariantMap metadata{
      {QStringLiteral("directory"), state.directory},
      {QStringLiteral("fps"), state.fps},
      {QStringLiteral("trimStart"), state.trim_start_seconds},
      {QStringLiteral("trimEnd"), state.trim_end_seconds},
      {QStringLiteral("frameCount"), state.frame_count},
      {QStringLiteral("sourceSignature"), state.source_signature},
  };
  return timelineForegroundMaskPath(metadata, state.source_signature, source_seconds,
                                    error_message);
}

QImage apply_timeline_foreground_matte(
    const QImage& source,
    const QString& mask_path,
    QString* error_message) {
  auto output = source;
  return applyTimelineForegroundMask(&output, mask_path, error_message)
             ? output
             : QImage{};
}

}  // namespace photo_editor::desktop
