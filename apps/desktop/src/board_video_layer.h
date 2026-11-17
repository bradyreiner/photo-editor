#pragma once

#include "app_bootstrap.h"
#include "photo_board_layout.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>

namespace photo_editor::desktop {

// Posters are only an editing proxy. The original movie and normalized source
// window travel with the layer; neither layout nor reframing rewrites media.
inline QVariantMap board_video_source_layer(const QString& path, const QString& name,
                                           const QString& asset_id, double seconds = 0) {
  const QFileInfo info(path);
  if (!info.isFile()) return {};
  const auto identity = info.absoluteFilePath() + QString::number(info.size()) +
      QString::number(info.lastModified().toMSecsSinceEpoch()) + QString::number(seconds, 'f', 6);
  const auto directory = QDir(AppBootstrap::appDataLocation()).filePath("carousels/video-frames");
  if (!QDir().mkpath(directory)) return {};
  const auto poster = QDir(directory).filePath(QString::fromLatin1(
      QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".png");
  if (!QImageReader(poster).size().isValid()) {
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    const auto pending = poster + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png";
    QProcess process;
    process.start(ffmpeg, {"-v", "error", "-y", "-ss", QString::number(qMax(0.0, seconds), 'f', 6),
        "-i", info.absoluteFilePath(), "-frames:v", "1", "-update", "1", pending});
    if (ffmpeg.isEmpty() || !process.waitForFinished(15000) || process.exitCode() != 0 ||
        !QImageReader(pending).size().isValid()) { QFile::remove(pending); return {}; }
    if (!QFile::rename(pending, poster)) QFile::remove(pending); // Another importer may have completed it.
  }
  const auto size = QImageReader(poster).size();
  if (!size.isValid()) return {};
  return {{"id", "board-video-" + QUuid::createUuid().toString(QUuid::WithoutBraces)},
      {"kind", "source"}, {"name", name.isEmpty() ? info.completeBaseName() : name},
      {"sourcePath", poster}, {"pixelWidth", size.width()}, {"pixelHeight", size.height()},
      {"sourcePixelWidth", size.width()}, {"sourcePixelHeight", size.height()},
      {"enabled", true}, {"opacity", 1.0}, {"blendMode", "normal"}, {"scale", 1.0}, {"x", 0}, {"y", 0},
      {"boardVideoSource", QVariantMap{{"filePath", info.absoluteFilePath()}, {"assetId", asset_id}}}};
}

inline QVariantMap board_video_frame(const QVariantMap& layer, const QSize& canvas) {
  const auto native = QSizeF(layer.value("pixelWidth").toDouble(), layer.value("pixelHeight").toDouble());
  const auto crop = board_photo_crop_bounds(layer, native);
  const auto scale = layer.value("scale", 1.0).toDouble();
  const QRectF frame(layer.value("x").toDouble() + crop.x() * scale,
      layer.value("y").toDouble() + crop.y() * scale, crop.width() * scale, crop.height() * scale);
  if (!native.isValid() || !frame.isValid() || !canvas.isValid()) return {};
  return {{"frameMode", "stretch"}, {"frameWidth", frame.width() / canvas.width()},
      {"frameHeight", frame.height() / canvas.height()}, {"frameScale", 1.0},
      {"frameOffsetX", (frame.center().x() / canvas.width() - .5) * 4},
      {"frameOffsetY", (frame.center().y() / canvas.height() - .5) * 4},
      {"frameRotationDegrees", layer.value("rotation", 0.0)},
      {"sourceCropLeft", crop.left() / native.width()}, {"sourceCropTop", crop.top() / native.height()},
      {"sourceCropRight", crop.right() / native.width()}, {"sourceCropBottom", crop.bottom() / native.height()}};
}

inline QVariantMap board_video_layer_from_clip(QVariantMap layer, const QVariantMap& clip, const QSize& canvas) {
  double left = clip.value("sourceCropLeft", 0.0).toDouble(), top = clip.value("sourceCropTop", 0.0).toDouble();
  double width = clip.value("sourceCropRight", 1.0).toDouble() - left;
  double height = clip.value("sourceCropBottom", 1.0).toDouble() - top;
  const auto native_width = layer.value("pixelWidth").toDouble(), native_height = layer.value("pixelHeight").toDouble();
  double frame_width = canvas.width() * clip.value("frameWidth", 1.0).toDouble() * clip.value("frameScale", 1.0).toDouble();
  double frame_height = canvas.height() * clip.value("frameHeight", 1.0).toDouble() * clip.value("frameScale", 1.0).toDouble();
  if (native_width <= 0 || native_height <= 0 || width <= 0 || height <= 0 || frame_width <= 0 || frame_height <= 0) return {};
  const auto fit = clip.value("frameMode").toString();
  if (fit == "fill") {
    const auto scale = qMax(frame_width / (native_width * width), frame_height / (native_height * height));
    const auto crop_width = frame_width / (native_width * scale), crop_height = frame_height / (native_height * scale);
    left += (width - crop_width) / 2; top += (height - crop_height) / 2;
    width = crop_width; height = crop_height;
  } else if (fit != "stretch") {
    const auto scale = qMin(frame_width / (native_width * width), frame_height / (native_height * height));
    frame_width = native_width * width * scale; frame_height = native_height * height * scale;
  }
  const auto scale = frame_width / (native_width * width);
  layer.insert("scale", scale);
  layer.insert("x", qRound(canvas.width() * (.5 + clip.value("frameOffsetX").toDouble() / 4) - frame_width / 2 - left * native_width * scale));
  layer.insert("y", qRound(canvas.height() * (.5 + clip.value("frameOffsetY").toDouble() / 4) - frame_height / 2 - top * native_height * scale));
  layer.insert("rotation", clip.value("frameRotationDegrees", 0.0));
  layer.insert("opacity", clip.value("opacity", 1.0));
  layer.insert("boardPhotoCrop", QVariantMap{{"x", left}, {"y", top}, {"width", width}, {"height", height}});
  return layer;
}

} // namespace photo_editor::desktop
