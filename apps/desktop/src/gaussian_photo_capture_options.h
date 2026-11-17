#pragma once
#include <QSet>
#include <QSize>
#include <QVariantMap>
#include <cmath>

namespace photo_editor::desktop {
// Sharing is an explicit user assertion, never inferred from missing EXIF.
// Reject contradictions that can be observed; metadata cannot establish that
// stabilization, digital crop or an unreported zoom stayed fixed.
inline QVariantMap gaussian_share_photo_intrinsics(QVariantMap prepared) {
  if (!prepared.value("ok").toBool()) return prepared;
  const auto reject = [](const QString& reason) {
    return QVariantMap{{"ok", false}, {"error", reason + " Turn off Same camera and zoom, or use matching photos."}};
  };
  const auto frames = prepared.value("frames").toList();
  if (frames.size() < 8 || frames.size() != prepared.value("frameCount").toInt())
    return reject("Cannot verify the fixed-camera photo set.");
  QSize prepared_size, source_size;
  QSet<QString> formats;
  QMap<QString, QString> identities;
  QMap<QString, double> focals;
  for (const auto& value : frames) {
    const auto frame = value.toMap();
    const QSize size(frame.value("width").toInt(), frame.value("height").toInt());
    const QSize original(frame.value("sourceWidth").toInt(), frame.value("sourceHeight").toInt());
    if (size.isEmpty() || original.isEmpty() || (!prepared_size.isEmpty() && size != prepared_size) ||
        (!source_size.isEmpty() && original != source_size))
      return reject("The photos have different or unavailable pixel dimensions.");
    prepared_size = size; source_size = original;
    const auto optics = frame.value("optics").toMap();
    const auto format = optics.value("sourceFormat").toString();
    if (format.isEmpty()) return reject("Cannot verify the photos' source format.");
    formats.insert(format);
    if (formats.size() != 1) return reject("The photos mix source formats or RAW and processed images.");
    for (const auto* key : {"make", "model", "bodySerial", "lens"}) {
      const auto text = optics.value(QLatin1String(key)).toString().trimmed();
      if (text.isEmpty()) continue;
      if (identities.contains(QLatin1String(key)) && identities.value(QLatin1String(key)) != text)
        return reject("The photos identify different cameras or lenses.");
      identities.insert(QLatin1String(key), text);
    }
    for (const auto* key : {"focalMm", "focal35Mm"}) {
      const auto focal = optics.value(QLatin1String(key)).toDouble();
      if (!std::isfinite(focal) || focal < 0) return reject("A photo has invalid focal-length metadata.");
      if (focal == 0) continue;
      if (focals.contains(QLatin1String(key)) && std::abs(focals.value(QLatin1String(key)) - focal) > 1e-6)
        return reject("The photos report different focal lengths or zoom settings.");
      focals.insert(QLatin1String(key), focal);
    }
  }
  prepared.insert("sourceCameraGroups", prepared.value("cameraGroups"));
  prepared.insert("sourceFocalPriorFrames", prepared.value("focalPriorFrames", 0));
  prepared.insert("cameraGroups", QVariantList{});
  prepared.insert("focalPriorFrames", 0);
  prepared.insert("estimatedIntrinsicsFrames", frames.size());
  prepared.insert("intrinsics", QVariantMap{{"contract", "user-asserted-fixed-camera-estimated-v1"},
      {"cameraModel", "SIMPLE_RADIAL"}, {"userAssertedSameCameraAndZoom", true},
      {"numericPriorSupplied", false}, {"calibrated", false},
      {"width", prepared_size.width()}, {"height", prepared_size.height()},
      {"sourceWidth", source_size.width()}, {"sourceHeight", source_size.height()}});
  return prepared;
}
}
