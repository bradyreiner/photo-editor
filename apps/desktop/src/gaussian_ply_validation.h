#pragma once
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QRegularExpression>
#include <QVariantMap>
#include <QtEndian>
#include <atomic>
#include <array>
#include <bit>
#include <cmath>
#include <memory>

namespace photo_editor::desktop {
inline QVariantMap validate_gaussian_ply(const QString& path, const std::shared_ptr<std::atomic_bool>& canceled) {
  const auto failed = [](const QString& error) { return QVariantMap{{"ok", false}, {"error", error}}; };
  const QFileInfo before(path);
  if (!before.isFile() || before.isSymLink()) return failed("The Gaussian artifact must be a regular project file.");
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return failed("The trainer did not produce a readable Gaussian artifact.");
  QByteArray header;
  while (header.size() < 16384 && !header.endsWith("end_header\n") && !header.endsWith("end_header\r\n")) {
    const auto line = file.readLine(16384 - header.size());
    if (line.isEmpty()) return failed("The Gaussian artifact has an incomplete header.");
    header += line;
  }
  auto normalized = header;
  normalized.replace("\r\n", "\n");
  if (!normalized.startsWith("ply\nformat binary_little_endian 1.0\n") || !normalized.endsWith("end_header\n"))
    return failed("Only standard little-endian Gaussian PLY is supported.");
  quint64 count = 0;
  QStringList properties;
  for (const auto& line : normalized.split('\n')) {
    if (line.startsWith("element vertex ")) {
      bool ok = false;
      if (count != 0) return failed("Duplicate vertex declaration in Gaussian artifact.");
      count = line.mid(15).toULongLong(&ok);
      if (!ok || count == 0 || count > 10000000) return failed("Invalid Gaussian count.");
    } else if (line.startsWith("element ")) return failed("Unexpected element in Gaussian artifact.");
    else if (line.startsWith("property ")) {
      const auto fields = line.simplified().split(' ');
      if (fields.size() != 3 || (fields[1] != "float" && fields[1] != "float32") || count == 0)
        return failed("Unsupported Gaussian property format.");
      const auto name = QString::fromLatin1(fields[2]);
      if (properties.contains(name)) return failed("Duplicate Gaussian property.");
      properties.append(name);
    }
  }
  for (const auto& required : {"x", "y", "z", "f_dc_0", "f_dc_1", "f_dc_2", "opacity",
                               "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"}) {
    if (!properties.contains(QString::fromLatin1(required))) return failed("The PLY is missing required Gaussian properties.");
  }
  if (properties.size() > 128 || count == 0) return failed("Unsupported Gaussian layout.");
  const auto row_bytes = static_cast<quint64>(properties.size()) * 4;
  const auto payload_bytes = count * row_bytes;
  if (payload_bytes > 2ULL * 1024ULL * 1024ULL * 1024ULL ||
      file.size() != header.size() + static_cast<qint64>(payload_bytes)) return failed("The Gaussian payload is truncated, oversized, or has unexpected trailing data.");
  QCryptographicHash hash(QCryptographicHash::Sha256);
  hash.addData(header);
  const std::array<qsizetype, 4> rotation_indices{properties.indexOf("rot_0"), properties.indexOf("rot_1"),
      properties.indexOf("rot_2"), properties.indexOf("rot_3")};
  quint64 remaining = count;
  while (remaining > 0) {
    if (canceled && canceled->load()) return failed("Gaussian validation canceled.");
    const auto rows = qMin(remaining, quint64{4096});
    const auto chunk = file.read(static_cast<qint64>(rows * row_bytes));
    if (static_cast<quint64>(chunk.size()) != rows * row_bytes) return failed("Cannot read the complete Gaussian payload.");
    hash.addData(chunk);
    for (quint64 row = 0; row < rows; ++row) {
      double rotation_norm = 0;
      for (qsizetype column = 0; column < properties.size(); ++column) {
        const auto offset = row * row_bytes + static_cast<quint64>(column) * 4;
        const float value = std::bit_cast<float>(qFromLittleEndian<quint32>(chunk.constData() + offset));
        if (!std::isfinite(value)) return failed("The trained Gaussian artifact contains non-finite values.");
        if (column == rotation_indices[0] || column == rotation_indices[1] ||
            column == rotation_indices[2] || column == rotation_indices[3])
          rotation_norm += static_cast<double>(value) * static_cast<double>(value);
      }
      if (rotation_norm < 1e-12) return failed("The trained Gaussian artifact contains an invalid rotation.");
    }
    remaining -= rows;
  }
  const QFileInfo after(path);
  if (after.isSymLink() || before.size() != after.size() || before.lastModified() != after.lastModified() ||
      before.canonicalFilePath() != after.canonicalFilePath())
    return failed("The Gaussian artifact changed during validation.");
  return {{"ok", true}, {"vertexCount", QVariant::fromValue(count)}, {"propertyCount", properties.size()},
      {"artifactBytes", file.size()}, {"artifactSha256", QString::fromLatin1(hash.result().toHex())}, {"artifactPath", path}};
}
}
