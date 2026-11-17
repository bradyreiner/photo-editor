#include "timeline_desktop_light_field.h"
#include "timeline_desktop_light_field_metal.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSize>
#include <QStringList>

#include <sys/resource.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>

namespace {

double smoothstep_quintic(double seconds, double start, double end) {
  const auto progress = std::clamp((seconds - start) / (end - start), 0.0, 1.0);
  const auto progress_squared = progress * progress;
  const auto progress_cubed = progress_squared * progress;
  return progress_cubed *
         (10.0 + progress * (-15.0 + 6.0 * progress));
}

double median(std::vector<double> values) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const auto middle = values.size() / 2;
  if (values.size() % 2 == 0) {
    return (values.at(middle - 1) + values.at(middle)) * 0.5;
  }
  return values.at(middle);
}

double percentile(std::vector<double> values, double quantile) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const auto index = static_cast<size_t>(std::ceil(quantile * values.size()) - 1.0);
  return values.at(std::min(index, values.size() - 1));
}

qint64 max_rss_bytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return -1;
  }
  return static_cast<qint64>(usage.ru_maxrss);
}

QString option_value(const QStringList& arguments, const QString& name,
                     const QString& fallback) {
  const auto index = arguments.indexOf(name);
  return index >= 0 && index + 1 < arguments.size() ? arguments.at(index + 1) : fallback;
}

QJsonObject run(const QStringList& arguments) {
  const auto backend = option_value(arguments, QStringLiteral("--backend"),
                                    QStringLiteral("metal")).trimmed().toLower();
  const auto frames = option_value(arguments, QStringLiteral("--frames"),
                                   QStringLiteral("300")).toInt();
  const auto fps = option_value(arguments, QStringLiteral("--fps"),
                                 QStringLiteral("30")).toDouble();
  const auto cache_path = option_value(arguments, QStringLiteral("--cache-dir"), {});
  const auto artifact_path = option_value(arguments, QStringLiteral("--artifacts"), {});
  constexpr QSize size(1920, 1080);
  constexpr double opacity = 0.7;
  constexpr qint64 maximum_cache_bytes = 2LL * 1024 * 1024 * 1024;
  if ((backend != QStringLiteral("cpu") && backend != QStringLiteral("metal")) ||
      frames <= 0 || frames > 900 || !std::isfinite(fps) || fps <= 0.0 ||
      cache_path.isEmpty()) {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral(
                 "Use --backend cpu|metal, --frames 1..900, positive --fps, and --cache-dir.")}};
  }
  if (backend == QStringLiteral("metal") &&
      !photo_editor::desktop::timeline_motion::desktop_light_field_metal_available()) {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Native Metal is unavailable.")}};
  }
  if (!QDir().mkpath(cache_path)) {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Cannot create the benchmark cache folder.")}};
  }
  if (!artifact_path.isEmpty() && !QDir().mkpath(artifact_path)) {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Cannot create the frame artifact folder.")}};
  }

  const auto memory_before = max_rss_bytes();
  QJsonArray frame_samples;
  QByteArray canonical_samples;
  for (int frame = 0; frame < frames; ++frame) {
    const auto seconds = static_cast<double>(frame) / fps;
    const auto gather = 1.0 - smoothstep_quintic(seconds, 1.0, 4.0);
    const auto sample_line = QStringLiteral("%1,%2,%3\n")
                                 .arg(frame)
                                 .arg(seconds, 0, 'f', 9)
                                 .arg(gather, 0, 'f', 15);
    canonical_samples.append(sample_line.toUtf8());
    frame_samples.append(QJsonObject{
        {QStringLiteral("frame"), frame},
        {QStringLiteral("timeSeconds"), seconds},
        {QStringLiteral("gather"), gather},
        {QStringLiteral("opacity"), opacity},
    });
  }
  const auto samples_hash = QString::fromLatin1(
      QCryptographicHash::hash(canonical_samples, QCryptographicHash::Sha256).toHex());

  std::vector<double> generation_ms;
  generation_ms.reserve(static_cast<size_t>(frames));
  qint64 png_write_ns = 0;
  qint64 cached_bytes = 0;
  quint64 pixel_checksum = 1469598103934665603ULL;
  const std::array<int, 5> review_frames{0, 60, 149, 210, 270};
  QElapsedTimer total_timer;
  total_timer.start();
  QString render_error;
  for (int frame = 0; frame < frames; ++frame) {
    const auto seconds = static_cast<double>(frame) / fps;
    const auto gather = 1.0 - smoothstep_quintic(seconds, 1.0, 4.0);
    const auto parameters = photo_editor::desktop::timeline_motion::DesktopLightFieldParameters{
        .time_seconds = seconds,
        .gather = gather,
        .opacity = opacity,
    };
    QElapsedTimer frame_timer;
    frame_timer.start();
    QImage image = backend == QStringLiteral("metal")
        ? photo_editor::desktop::timeline_motion::render_desktop_light_field_metal(
              size, parameters, nullptr, &render_error)
        : photo_editor::desktop::timeline_motion::render_desktop_light_field(size, parameters);
    generation_ms.push_back(static_cast<double>(frame_timer.nsecsElapsed()) / 1000000.0);
    if (image.isNull()) {
      return {{QStringLiteral("ok"), false},
              {QStringLiteral("error"), render_error.isEmpty()
                   ? QStringLiteral("Frame generation failed.")
                   : render_error}};
    }
    const auto* pixels = image.constBits();
    for (qsizetype byte = 0; byte < image.sizeInBytes(); byte += 4093) {
      pixel_checksum ^= static_cast<quint8>(pixels[byte]);
      pixel_checksum *= 1099511628211ULL;
    }

    const auto frame_path = QDir(cache_path).filePath(
        QStringLiteral("%1.png").arg(frame, 6, 10, QLatin1Char('0')));
    QElapsedTimer write_timer;
    write_timer.start();
    QSaveFile cache_file(frame_path);
    if (!cache_file.open(QIODevice::WriteOnly) ||
        !image.save(&cache_file, "PNG") || !cache_file.commit()) {
      return {{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("Native lossless PNG cache write failed.")}};
    }
    png_write_ns += write_timer.nsecsElapsed();
    cached_bytes += QFileInfo(frame_path).size();
    if (cached_bytes > maximum_cache_bytes) {
      return {{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("The generated cache exceeded its 2 GiB benchmark bound.")}};
    }
    if (!artifact_path.isEmpty() &&
        std::find(review_frames.cbegin(), review_frames.cend(), frame) != review_frames.cend()) {
      const auto review_path = QDir(artifact_path).filePath(
          QStringLiteral("%1_%2.png").arg(backend).arg(frame, 6, 10, QLatin1Char('0')));
      if (!image.save(review_path, "PNG")) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("A full-frame review artifact could not be saved.")}};
      }
    }
  }
  const auto cold_total_ms = total_timer.elapsed();

  total_timer.restart();
  quint64 read_checksum = 1469598103934665603ULL;
  for (int frame = 0; frame < frames; ++frame) {
    const auto frame_path = QDir(cache_path).filePath(
        QStringLiteral("%1.png").arg(frame, 6, 10, QLatin1Char('0')));
    QImageReader reader(frame_path, "PNG");
    const auto image = reader.read().convertToFormat(QImage::Format_RGBA8888);
    if (image.size() != size) {
      return {{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("A cached full-resolution source frame failed verification.")}};
    }
    const auto* pixels = image.constBits();
    for (qsizetype byte = 0; byte < image.sizeInBytes(); byte += 4093) {
      read_checksum ^= static_cast<quint8>(pixels[byte]);
      read_checksum *= 1099511628211ULL;
    }
  }
  const auto warm_cache_ms = total_timer.elapsed();
  const auto memory_peak = max_rss_bytes();

  QJsonObject result{
      {QStringLiteral("ok"), true},
      {QStringLiteral("backend"), backend},
      {QStringLiteral("width"), size.width()},
      {QStringLiteral("height"), size.height()},
      {QStringLiteral("fps"), fps},
      {QStringLiteral("frames"), frames},
      {QStringLiteral("sampleSha256"), samples_hash},
      {QStringLiteral("frameSamples"), frame_samples},
      {QStringLiteral("generationMedianMs"), median(generation_ms)},
      {QStringLiteral("generationP95Ms"), percentile(generation_ms, 0.95)},
      {QStringLiteral("generationMaxMs"), *std::max_element(generation_ms.begin(), generation_ms.end())},
      {QStringLiteral("generationTotalMs"), std::accumulate(generation_ms.begin(), generation_ms.end(), 0.0)},
      {QStringLiteral("pngWriteTotalMs"), static_cast<double>(png_write_ns) / 1000000.0},
      {QStringLiteral("cacheColdTotalMs"), cold_total_ms},
      {QStringLiteral("cacheWarmReadMs"), warm_cache_ms},
      {QStringLiteral("cacheBytes"), cached_bytes},
      {QStringLiteral("cacheLimitBytes"), maximum_cache_bytes},
      {QStringLiteral("cacheCodec"), QStringLiteral("PNG via QImage::save default lossless settings")},
      {QStringLiteral("pixelChecksum"), QString::number(pixel_checksum, 16)},
      {QStringLiteral("cacheReadChecksum"), QString::number(read_checksum, 16)},
      {QStringLiteral("peakRssBytes"), memory_peak},
      {QStringLiteral("rssDeltaBytes"), memory_peak >= memory_before ? memory_peak - memory_before : -1},
      {QStringLiteral("frameOutputBytes"), static_cast<double>(size.width()) * size.height() * 4.0},
  };
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  const auto result = run(application.arguments());
  std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
  return result.value(QStringLiteral("ok")).toBool() ? 0 : 1;
}
