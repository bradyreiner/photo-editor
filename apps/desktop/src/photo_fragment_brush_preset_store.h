#pragma once

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVariantMap>

#include "photo_pixel_stroke.h"

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {

class PhotoFragmentBrushPresetStore final {
 public:
  static QString directoryPath() {
    auto root = qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_DIR").trimmed();
    if (root.isEmpty()) root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir::cleanPath(QDir(root).filePath(QStringLiteral("brush-presets/photo-fragments")));
  }

  static QStringList names() {
    QDir directory(directoryPath());
    const auto files = directory.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    QStringList result;
    for (const auto& file : files) {
      const auto preset = readFile(file.absoluteFilePath());
      const auto name = preset.value(QStringLiteral("name")).toString().simplified();
      if (!name.isEmpty() && !result.contains(name, Qt::CaseInsensitive)) result.push_back(name);
    }
    return result;
  }

  static QVariantMap load(const QString& name, QString* error = nullptr) {
    const auto normalized = normalizeName(name);
    if (normalized.isEmpty()) return fail(error, QStringLiteral("Photo-fragment preset name is invalid."));
    const auto preset = readFile(presetPath(normalized));
    if (preset.isEmpty()) return fail(error, QStringLiteral("Photo-fragment preset was not found or is invalid."));
    return preset;
  }

  static QVariantMap loadForBrush(const QString& name, QImage* image, QString* error = nullptr) {
    const auto normalized = normalizeName(name);
    if (normalized.isEmpty() || image == nullptr)
      return fail(error, QStringLiteral("Photo-fragment preset name is invalid."));
    const auto path = presetPath(normalized);
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable())
      return fail(error, QStringLiteral("Photo-fragment preset was not found or is invalid."));
    const auto modified = info.lastModified().toMSecsSinceEpoch();
    {
      QMutexLocker lock(&imageCacheMutex());
      const auto& cache = decodedImageCache();
      if (cache.path == path && cache.file_size == info.size() &&
          cache.modified_ms == modified && !cache.image.isNull()) {
        *image = cache.image;
        return cache.metadata;
      }
    }
    auto preset = readFile(path);
    if (preset.isEmpty())
      return fail(error, QStringLiteral("Photo-fragment preset was not found or is invalid."));
    auto decoded = imageFromPreset(preset);
    if (decoded.isNull())
      return fail(error, QStringLiteral("The selected Photo Fragment preset could not be read."));
    preset.remove(QStringLiteral("imagePngBase64"));
    rememberDecodedImage(path, info.size(), modified, preset, decoded);
    *image = std::move(decoded);
    return preset;
  }

  static void clearDecodedImageCache() {
    QMutexLocker lock(&imageCacheMutex());
    decodedImageCache() = {};
  }

  static bool save(
      const QString& name, const QImage& image, double rotation, double scale,
      bool overwrite, QString* error = nullptr) {
    const auto normalized = normalizeName(name);
    if (normalized.isEmpty()) return failBool(error, QStringLiteral("Photo-fragment preset name must be 1 to 64 characters."));
    if (image.isNull() || image.width() > 4096 || image.height() > 4096 ||
        static_cast<qint64>(image.width()) * image.height() > 16000000)
      return failBool(error, QStringLiteral("Photo-fragment preset image must fit within 16 megapixels and 4096 pixels per side."));
    if (!std::isfinite(rotation) || rotation < 0 || rotation > 180 ||
        !std::isfinite(scale) || scale < 0 || scale > 0.9)
      return failBool(error, QStringLiteral("Photo-fragment variation settings are outside their supported range."));
    const auto path = presetPath(normalized);
    if (!overwrite && QFileInfo::exists(path))
      return failBool(error, QStringLiteral("A photo-fragment preset with that name already exists."));
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG") || png.size() > kMaxPngBytes)
      return failBool(error, QStringLiteral("The photo-fragment image could not be encoded within the preset size limit."));
    const QJsonObject object{{QStringLiteral("version"), 1}, {QStringLiteral("name"), normalized},
        {QStringLiteral("imagePngBase64"), QString::fromLatin1(png.toBase64())},
        {QStringLiteral("rotationVariation"), rotation}, {QStringLiteral("scaleVariation"), scale}};
    if (!writeFile(path, QJsonDocument(object).toJson(QJsonDocument::Compact), error)) return false;
    PhotoPixelStroke::clearTipMipCache();
    const auto info = QFileInfo(path);
    const auto decoded = QImage::fromData(png, "PNG");
    if (!decoded.isNull()) {
      rememberDecodedImage(path, info.size(), info.lastModified().toMSecsSinceEpoch(),
          {{QStringLiteral("name"), normalized},
           {QStringLiteral("rotationVariation"), rotation},
           {QStringLiteral("scaleVariation"), scale}}, decoded);
    }
    return true;
  }

  static bool exportFile(const QString& name, const QString& path, QString* error = nullptr) {
    const auto preset = load(name, error);
    if (preset.isEmpty()) return false;
    return writeFile(path, encode(preset), error);
  }

  static bool remove(const QString& name) {
    const auto normalized = normalizeName(name);
    if (normalized.isEmpty()) return false;
    const auto path = presetPath(normalized);
    const auto removed = QFile::remove(path);
    if (removed) {
      QMutexLocker lock(&imageCacheMutex());
      if (decodedImageCache().path == path) decodedImageCache() = {};
      PhotoPixelStroke::clearTipMipCache();
    }
    return removed;
  }

  static bool importFile(const QString& path, bool overwrite, QString* error = nullptr) {
    const auto preset = readFile(path);
    if (preset.isEmpty()) return failBool(error, QStringLiteral("The selected file is not a valid Photo Fragment preset."));
    return save(preset.value(QStringLiteral("name")).toString(), imageFromPreset(preset),
        preset.value(QStringLiteral("rotationVariation")).toDouble(),
        preset.value(QStringLiteral("scaleVariation")).toDouble(), overwrite, error);
  }

  static QImage imageFromPreset(const QVariantMap& preset) {
    const auto encoded = QByteArray::fromBase64Encoding(
        preset.value(QStringLiteral("imagePngBase64")).toString().toLatin1());
    if (encoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok || encoded.decoded.size() > kMaxPngBytes)
      return {};
    QBuffer buffer;
    buffer.setData(encoded.decoded);
    if (!buffer.open(QIODevice::ReadOnly)) return {};
    QImageReader reader(&buffer, "PNG");
    const auto size = reader.size();
    if (size.isEmpty() || size.width() > 4096 || size.height() > 4096 ||
        static_cast<qint64>(size.width()) * size.height() > 16000000) return {};
    return reader.read();
  }

  static QImage cropToVisiblePixels(const QImage& source) {
    if (source.isNull()) return {};
    const auto alpha = source.convertToFormat(QImage::Format_ARGB32);
    int left = alpha.width(), top = alpha.height(), right = -1, bottom = -1;
    for (int y = 0; y < alpha.height(); ++y) {
      const auto* row = reinterpret_cast<const QRgb*>(alpha.constScanLine(y));
      for (int x = 0; x < alpha.width(); ++x) {
        if (qAlpha(row[x]) == 0) continue;
        left = std::min(left, x); top = std::min(top, y);
        right = std::max(right, x); bottom = std::max(bottom, y);
      }
    }
    const QRect bounds(QPoint(left, top), QPoint(right, bottom));
    if (bounds.isEmpty() || bounds.width() > 4096 || bounds.height() > 4096 ||
        static_cast<qint64>(bounds.width()) * bounds.height() > 16000000) return {};
    return source.copy(bounds);
  }

 private:
  static constexpr qsizetype kMaxPngBytes = 32 * 1024 * 1024;

  struct DecodedImageCache {
    QString path;
    qint64 file_size = -1;
    qint64 modified_ms = -1;
    QVariantMap metadata;
    QImage image;
  };

  static QMutex& imageCacheMutex() {
    static QMutex mutex;
    return mutex;
  }

  static DecodedImageCache& decodedImageCache() {
    static DecodedImageCache cache;
    return cache;
  }

  static void rememberDecodedImage(const QString& path, qint64 size, qint64 modified_ms,
      const QVariantMap& metadata, const QImage& image) {
    QMutexLocker lock(&imageCacheMutex());
    decodedImageCache() = {path, size, modified_ms, metadata, image};
  }

  static QString normalizeName(const QString& name) {
    const auto normalized = name.simplified();
    return normalized.size() <= 64 ? normalized : QString{};
  }

  static QString presetPath(const QString& name) {
    const auto key = QCryptographicHash::hash(name.toCaseFolded().toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(directoryPath()).filePath(QString::fromLatin1(key) + QStringLiteral(".json"));
  }

  static QByteArray encode(const QVariantMap& preset) {
    return QJsonDocument::fromVariant(preset).toJson(QJsonDocument::Compact);
  }

  static QVariantMap readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxPngBytes * 2) return {};
    QJsonParseError parse_error{};
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) return {};
    const auto object = document.object();
    if (object.value(QStringLiteral("version")).toInt() != 1 ||
        normalizeName(object.value(QStringLiteral("name")).toString()).isEmpty() ||
        !object.value(QStringLiteral("imagePngBase64")).isString()) return {};
    const auto preset = object.toVariantMap();
    if (!imagePayloadHasValidHeader(preset)) return {};
    const auto rotation = preset.value(QStringLiteral("rotationVariation")).toDouble();
    const auto scale = preset.value(QStringLiteral("scaleVariation")).toDouble();
    if (!std::isfinite(rotation) || rotation < 0 || rotation > 180 ||
        !std::isfinite(scale) || scale < 0 || scale > 0.9) return {};
    return preset;
  }

  static bool writeFile(const QString& path, const QByteArray& data, QString* error) {
    if (path.isEmpty()) return failBool(error, QStringLiteral("A destination path is required."));
    const auto parent = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(parent)) return failBool(error, QStringLiteral("Could not create the Photo Fragment preset directory."));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
      return failBool(error, QStringLiteral("Could not write the Photo Fragment preset file."));
    return true;
  }

  static bool imagePayloadHasValidHeader(const QVariantMap& preset) {
    const auto encoded = QByteArray::fromBase64Encoding(
        preset.value(QStringLiteral("imagePngBase64")).toString().toLatin1());
    if (encoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok ||
        encoded.decoded.size() > kMaxPngBytes) return false;
    QBuffer buffer;
    buffer.setData(encoded.decoded);
    if (!buffer.open(QIODevice::ReadOnly)) return false;
    QImageReader reader(&buffer, "PNG");
    const auto size = reader.size();
    return !size.isEmpty() && size.width() <= 4096 && size.height() <= 4096 &&
        static_cast<qint64>(size.width()) * size.height() <= 16000000;
  }

  static QVariantMap fail(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
    return {};
  }

  static bool failBool(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
    return false;
  }
};

}  // namespace photo_editor::desktop
