#include "recipe_asset_store.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaType>
#include <QRegularExpression>
#include <QSaveFile>

#include <cmath>
#include <utility>

namespace photo_editor::desktop {
namespace {

const QRegularExpression& asset_pattern() {
  static const QRegularExpression pattern(
      QStringLiteral("^sha256:([0-9a-f]{64})$"));
  return pattern;
}

QString digest_for_payload(const QByteArray& payload) {
  return QString::fromLatin1(
      QCryptographicHash::hash(
          payload,
          QCryptographicHash::Sha256)
          .toHex());
}

std::optional<QJsonValue> encode_variant(
    const QVariant& value,
    QString* error,
    const QString& path) {
  QJsonObject encoded;
  if (!value.isValid() || value.isNull()) {
    encoded.insert(QStringLiteral("type"),
                   QStringLiteral("null"));
    return encoded;
  }
  const auto type = value.metaType().id();
  if (type == QMetaType::QByteArray) {
    encoded.insert(QStringLiteral("type"),
                   QStringLiteral("bytes"));
    encoded.insert(
        QStringLiteral("value"),
        QString::fromLatin1(
            value.toByteArray().toBase64()));
    return encoded;
  }
  if (type == QMetaType::Bool) {
    encoded.insert(QStringLiteral("type"),
                   QStringLiteral("bool"));
    encoded.insert(QStringLiteral("value"),
                   value.toBool());
    return encoded;
  }
  if (type == QMetaType::Int ||
      type == QMetaType::LongLong) {
    encoded.insert(QStringLiteral("type"),
                   QStringLiteral("integer"));
    encoded.insert(
        QStringLiteral("value"),
        QString::number(value.toLongLong()));
    return encoded;
  }
  if (type == QMetaType::UInt ||
      type == QMetaType::ULongLong) {
    encoded.insert(QStringLiteral("type"),
                   QStringLiteral("unsigned"));
    encoded.insert(
        QStringLiteral("value"),
        QString::number(value.toULongLong()));
    return encoded;
  }
  if (type == QMetaType::Double ||
      type == QMetaType::Float) {
    bool ok = false;
    const auto number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number)) {
      if (error != nullptr) {
        *error =
            QStringLiteral(
                "Recipe asset contains a non-finite number at %1.")
                .arg(path);
      }
      return std::nullopt;
    }
    encoded.insert(QStringLiteral("type"),
                   QStringLiteral("number"));
    encoded.insert(QStringLiteral("value"),
                   number);
    return encoded;
  }
  if (type == QMetaType::QString) {
    encoded.insert(QStringLiteral("type"),
                   QStringLiteral("string"));
    encoded.insert(QStringLiteral("value"),
                   value.toString());
    return encoded;
  }
  if (type == QMetaType::QVariantMap) {
    QJsonObject values;
    const auto map = value.toMap();
    for (auto iterator = map.cbegin();
         iterator != map.cend();
         ++iterator) {
      const auto child = encode_variant(
          iterator.value(),
          error,
          path + QLatin1Char('.') + iterator.key());
      if (!child.has_value()) {
        return std::nullopt;
      }
      values.insert(iterator.key(), *child);
    }
    encoded.insert(QStringLiteral("type"),
                   QStringLiteral("map"));
    encoded.insert(QStringLiteral("value"), values);
    return encoded;
  }
  if (type == QMetaType::QVariantList ||
      type == QMetaType::QStringList) {
    QJsonArray values;
    const auto list = value.toList();
    for (qsizetype index = 0; index < list.size();
         ++index) {
      const auto child = encode_variant(
          list.at(index),
          error,
          QStringLiteral("%1[%2]")
              .arg(path)
              .arg(index));
      if (!child.has_value()) {
        return std::nullopt;
      }
      values.push_back(*child);
    }
    encoded.insert(QStringLiteral("type"),
                   QStringLiteral("list"));
    encoded.insert(QStringLiteral("value"), values);
    return encoded;
  }
  if (error != nullptr) {
    *error =
        QStringLiteral(
            "Recipe asset contains unsupported type %1 at %2.")
            .arg(
                QString::fromLatin1(
                    value.metaType().name()),
                path);
  }
  return std::nullopt;
}

std::optional<QVariant> decode_variant(
    const QJsonValue& value,
    QString* error,
    const QString& path) {
  if (!value.isObject()) {
    if (error != nullptr) {
      *error =
          QStringLiteral(
              "Recipe asset value at %1 is not tagged.")
              .arg(path);
    }
    return std::nullopt;
  }
  const auto object = value.toObject();
  const auto type =
      object.value(QStringLiteral("type")).toString();
  const auto payload =
      object.value(QStringLiteral("value"));
  if (type == QStringLiteral("null")) {
    return QVariant{};
  }
  if (type == QStringLiteral("bytes") &&
      payload.isString()) {
    const auto encoded =
        payload.toString().toLatin1();
    const auto decoded =
        QByteArray::fromBase64Encoding(
            encoded,
            QByteArray::AbortOnBase64DecodingErrors);
    if (decoded.decodingStatus !=
        QByteArray::Base64DecodingStatus::Ok) {
      if (error != nullptr) {
        *error =
            QStringLiteral(
                "Recipe asset bytes at %1 are not valid base64.")
                .arg(path);
      }
      return std::nullopt;
    }
    return QVariant(decoded.decoded);
  }
  if (type == QStringLiteral("bool") &&
      payload.isBool()) {
    return QVariant(payload.toBool());
  }
  if ((type == QStringLiteral("integer") ||
       type == QStringLiteral("unsigned")) &&
      payload.isString()) {
    bool ok = false;
    if (type == QStringLiteral("integer")) {
      const auto number =
          payload.toString().toLongLong(&ok);
      if (ok) {
        return QVariant::fromValue(number);
      }
    } else {
      const auto number =
          payload.toString().toULongLong(&ok);
      if (ok) {
        return QVariant::fromValue(number);
      }
    }
  }
  if (type == QStringLiteral("number") &&
      payload.isDouble()) {
    return QVariant(payload.toDouble());
  }
  if (type == QStringLiteral("string") &&
      payload.isString()) {
    return QVariant(payload.toString());
  }
  if (type == QStringLiteral("map") &&
      payload.isObject()) {
    QVariantMap map;
    const auto values = payload.toObject();
    for (auto iterator = values.begin();
         iterator != values.end();
         ++iterator) {
      const auto child = decode_variant(
          iterator.value(),
          error,
          path + QLatin1Char('.') + iterator.key());
      if (!child.has_value()) {
        return std::nullopt;
      }
      map.insert(iterator.key(), *child);
    }
    return QVariant(map);
  }
  if (type == QStringLiteral("list") &&
      payload.isArray()) {
    QVariantList list;
    const auto values = payload.toArray();
    list.reserve(values.size());
    for (qsizetype index = 0; index < values.size();
         ++index) {
      const auto child = decode_variant(
          values.at(index),
          error,
          QStringLiteral("%1[%2]")
              .arg(path)
              .arg(index));
      if (!child.has_value()) {
        return std::nullopt;
      }
      list.push_back(*child);
    }
    return QVariant(list);
  }
  if (error != nullptr) {
    *error =
        QStringLiteral(
            "Recipe asset tag or value at %1 is invalid.")
            .arg(path);
  }
  return std::nullopt;
}

}  // namespace

RecipeAssetStore::RecipeAssetStore(QString root_path)
    : root_path_(
          QDir::cleanPath(std::move(root_path))) {}

RecipeAssetWriteResult RecipeAssetStore::put(
    const QByteArray& payload) const {
  RecipeAssetWriteResult result;
  if (root_path_.trimmed().isEmpty()) {
    result.error =
        QStringLiteral(
            "Recipe asset store root is empty.");
    return result;
  }
  const auto digest = digest_for_payload(payload);
  const auto path = pathForDigest(digest);
  const QFileInfo info(path);
  if (!QDir().mkpath(info.dir().absolutePath())) {
    result.error =
        QStringLiteral(
            "Unable to create recipe asset directory.");
    return result;
  }

  if (info.exists()) {
    QString read_error;
    const auto existing = get(
        photo_editor::pipeline::AssetIdentifier{
            "sha256:" + digest.toStdString()},
        &read_error);
    if (!existing.has_value()) {
      result.error =
          read_error.isEmpty()
              ? QStringLiteral(
                    "Existing recipe asset is invalid.")
              : read_error;
      return result;
    }
    if (*existing != payload) {
      result.error =
          QStringLiteral(
              "Recipe asset digest collision.");
      return result;
    }
  } else {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(payload) != payload.size() ||
        !file.commit()) {
      result.error =
          QStringLiteral(
              "Unable to atomically store recipe asset.");
      return result;
    }
  }

  result.asset =
      photo_editor::pipeline::AssetIdentifier{
          "sha256:" + digest.toStdString()};
  return result;
}

RecipeAssetWriteResult RecipeAssetStore::putVariant(
    const QVariant& value) const {
  QString error;
  const auto encoded =
      encode_variant(
          value,
          &error,
          QStringLiteral("$"));
  if (!encoded.has_value()) {
    return RecipeAssetWriteResult{
        .error = error,
    };
  }
  QJsonObject envelope{
      {QStringLiteral("format"),
       QStringLiteral(
           "photo-editor-recipe-asset-v1")},
      {QStringLiteral("payload"), *encoded},
  };
  return put(
      QJsonDocument(envelope)
          .toJson(QJsonDocument::Compact));
}

std::optional<QByteArray> RecipeAssetStore::get(
    const photo_editor::pipeline::AssetIdentifier&
        asset,
    QString* error) const {
  if (error != nullptr) {
    error->clear();
  }
  const auto identifier =
      QString::fromStdString(asset.value)
          .trimmed()
          .toLower();
  const auto match =
      asset_pattern().match(identifier);
  if (!match.hasMatch()) {
    if (error != nullptr) {
      *error =
          QStringLiteral(
              "Recipe asset identifier is not canonical SHA-256.");
    }
    return std::nullopt;
  }
  const auto digest = match.captured(1);
  QFile file(pathForDigest(digest));
  if (!file.open(QIODevice::ReadOnly)) {
    if (error != nullptr) {
      *error =
          QStringLiteral(
              "Recipe asset payload is unavailable.");
    }
    return std::nullopt;
  }
  const auto payload = file.readAll();
  if (digest_for_payload(payload) != digest) {
    if (error != nullptr) {
      *error =
          QStringLiteral(
              "Recipe asset payload failed SHA-256 verification.");
    }
    return std::nullopt;
  }
  return payload;
}

std::optional<QVariant> RecipeAssetStore::getVariant(
    const photo_editor::pipeline::AssetIdentifier&
        asset,
    QString* error) const {
  const auto bytes = get(asset, error);
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  QJsonParseError parse_error;
  const auto document =
      QJsonDocument::fromJson(*bytes, &parse_error);
  if (!document.isObject() ||
      parse_error.error !=
          QJsonParseError::NoError) {
    if (error != nullptr) {
      *error =
          QStringLiteral(
              "Recipe asset envelope is not valid JSON.");
    }
    return std::nullopt;
  }
  const auto envelope = document.object();
  if (envelope
          .value(QStringLiteral("format"))
          .toString() !=
      QStringLiteral(
          "photo-editor-recipe-asset-v1")) {
    if (error != nullptr) {
      *error =
          QStringLiteral(
              "Recipe asset envelope version is unsupported.");
    }
    return std::nullopt;
  }
  return decode_variant(
      envelope.value(QStringLiteral("payload")),
      error,
      QStringLiteral("$"));
}

QString RecipeAssetStore::rootPath() const {
  return root_path_;
}

QString RecipeAssetStore::pathForDigest(
    const QString& digest) const {
  return QDir(root_path_)
      .filePath(
          QStringLiteral("sha256/%1/%2.bin")
              .arg(digest.left(2), digest));
}

}  // namespace photo_editor::desktop
