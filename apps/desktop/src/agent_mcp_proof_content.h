#pragma once

#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QJsonObject>

namespace photo_editor::desktop {

// Called only with a successful app-rendered proof path, never a client path.
// Bound decoding and transport size independently of the full-resolution artifact.
inline QJsonObject agent_mcp_proof_image(const QString& path, QJsonObject* receipt) {
  const auto unavailable = [&](const QString& reason) {
    *receipt = QJsonObject{{"available", false}, {"reason", reason}};
    return QJsonObject{};
  };
  QFile file(path);
  if (path.isEmpty() || !file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024)
    return unavailable("The app-rendered proof is missing, unreadable or exceeds the 8 MiB image limit.");
  const auto bytes = file.readAll();
  QBuffer source;
  source.setData(bytes);
  source.open(QIODevice::ReadOnly);
  QImageReader reader(&source, "png");
  const auto size = reader.size();
  if (!size.isValid() || size.width() > 4096 || size.height() > 4096)
    return unavailable("The app-rendered proof is not a bounded PNG image.");
  if (size.width() > 1024 || size.height() > 1024)
    reader.setScaledSize(size.scaled(1024, 1024, Qt::KeepAspectRatio));
  const auto preview = reader.read();
  if (preview.isNull()) return unavailable("The app-rendered PNG proof could not be decoded.");
  QByteArray encoded;
  QBuffer output(&encoded);
  output.open(QIODevice::WriteOnly);
  if (!preview.save(&output, "PNG") || encoded.size() > 8 * 1024 * 1024)
    return unavailable("The MCP proof preview could not be encoded within the image limit.");
  *receipt = QJsonObject{{"available", true}, {"width", preview.width()}, {"height", preview.height()},
      {"sourceSha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
      {"previewSha256", QString::fromLatin1(QCryptographicHash::hash(encoded, QCryptographicHash::Sha256).toHex())},
      {"basis", "bounded-preview-of-currentProofPath"}};
  return QJsonObject{{"type", "image"}, {"mimeType", "image/png"}, {"data", QString::fromLatin1(encoded.toBase64())}};
}

}  // namespace photo_editor::desktop
