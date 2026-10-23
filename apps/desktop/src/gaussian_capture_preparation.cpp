#include "gaussian_capture_preparation.h"
#include "photo_render_utils.h"
#include "render_plan_store.h"
#include "raw_image_decoder.h"
#include "capture_camera_metadata.h"
#include "gaussian_camera_priors.h"
#include "gaussian_timed_capture_manifest.h"
#include <QSaveFile>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonParseError>

namespace photo_editor::desktop {
QVariantMap validate_prepared_gaussian_timed_capture(const QString& project,
    const std::shared_ptr<std::atomic_bool>& canceled) {
  const auto reject = [](const QString& error) { return QVariantMap{{"ok", false}, {"error", error}}; };
  const auto stopped = [&] { return canceled && canceled->load(); };
  if (stopped()) return reject("Timed capture verification canceled.");
  const auto root = QFileInfo(project).canonicalFilePath();
  const auto path = QDir(root).filePath("timed-capture.json");
  QFile file(path);
  if (root.isEmpty() || QFileInfo(path).canonicalFilePath() != path || !file.open(QIODevice::ReadOnly) ||
      file.size() <= 0 || file.size() > 16 * 1024 * 1024) return reject("Cannot read the prepared timed capture.");
  const auto bytes = file.readAll();
  QJsonParseError parse;
  const auto document = QJsonDocument::fromJson(bytes, &parse);
  const auto dataset = document.toVariant().toMap();
  if (parse.error != QJsonParseError::NoError || !document.isObject() ||
      dataset.value("schema") != "framehouse-prepared-timed-capture-v1" ||
      dataset.value("state") != "prepared-not-trained" ||
      dataset.value("colorContract") != "canonical-decode-and-render-to-srgb-v1")
    return reject("Unsupported prepared timed-capture format or color contract.");
  const auto observations = dataset.value("observations").toList();
  if (observations.size() < 4 || observations.size() > 20000) return reject("Invalid timed observation count.");
  QVariantList times;
  QVariantList images;
  QVariant timestamp;
  int time_index = -1;
  const auto flush = [&] { if (!images.isEmpty()) times.append(QVariantMap{{"timeSeconds", timestamp}, {"images", images}}); };
  for (const auto& entry : observations) {
    if (stopped()) return reject("Timed capture verification canceled.");
    const auto observation = entry.toMap();
    const auto index = observation.value("timeIndex");
    // Require contiguous, ordered integer time indices, not coercible strings.
    const auto numeric = index.metaType().id() == QMetaType::LongLong || index.metaType().id() == QMetaType::Double;
    if (!numeric || !std::isfinite(index.toDouble()) || index.toDouble() != std::floor(index.toDouble()) ||
        index.toDouble() < 0 || index.toDouble() > 9999) return reject("Invalid prepared time index.");
    if (index.toInt() != time_index) {
      if (index.toInt() != time_index + 1) return reject("Prepared time indices must be contiguous and ordered.");
      flush(); images.clear(); timestamp = observation.value("timeSeconds"); ++time_index;
    } else if (observation.value("timeSeconds") != timestamp) return reject("Camera observations disagree on their capture time.");
    images.append(QVariantMap{{"cameraId", observation.value("cameraId")}, {"file", observation.value("file")}});
  }
  flush();
  auto input = dataset;
  input.insert("schema", "framehouse-timed-capture-v1"); input.insert("frames", times);
  const auto valid = validate_gaussian_timed_capture(input, root, canceled);
  if (!valid.valid()) return reject(valid.error);
  if (dataset.value("timeCount").toDouble() != valid.time_count ||
      dataset.value("durationSeconds").toDouble() != valid.duration_seconds)
    return reject("Prepared timing summary disagrees with its observations.");
  QHash<QString, QSize> sizes;
  for (const auto& entry : valid.cameras) {
    const auto camera = entry.toMap();
    const QSize size(camera.value("width").toInt(), camera.value("height").toInt());
    if (size.width() > 4096 || size.height() > 4096) return reject("Prepared image exceeds the supported resolution.");
    sizes.insert(camera.value("id").toString(), size);
  }
  for (qsizetype i = 0; i < observations.size(); ++i) {
    if (stopped()) return reject("Timed capture verification canceled.");
    QFile image(valid.sources[i]);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!image.open(QIODevice::ReadOnly)) return reject("Cannot read a prepared timed image.");
    while (!image.atEnd()) {
      if (stopped()) return reject("Timed capture verification canceled.");
      const auto block = image.read(4 * 1024 * 1024);
      if (block.isEmpty()) return reject("Cannot hash the complete prepared image.");
      hash.addData(block);
    }
    const auto observation = observations[i].toMap();
    if (QString::fromLatin1(hash.result().toHex()) != observation.value("preparedSha256").toString())
      return reject("A prepared timed image has changed. The project was not modified.");
    if (!image.seek(0)) return reject("Cannot verify prepared image pixels.");
    QImageReader reader(&image, "PNG");
    const auto expected_size = sizes.value(observation.value("cameraId").toString());
    if (reader.size() != expected_size)
      return reject("Prepared image dimensions do not match its calibration.");
    const auto decoded = reader.read();
    if (decoded.isNull() || decoded.size() != expected_size || decoded.colorSpace() != QColorSpace(QColorSpace::SRgb))
      return reject("Prepared image pixels or sRGB profile are invalid.");
  }
  if (stopped()) return reject("Timed capture verification canceled.");
  return {{"ok", true}, {"state", "prepared-not-trained"}, {"mode", "timed-4d-input"},
      {"cameraCount", valid.cameras.size()}, {"timeCount", valid.time_count}, {"imageCount", observations.size()},
      {"durationSeconds", valid.duration_seconds}, {"manifestPath", path},
      {"manifestSha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
      {"trainingAvailable", false}, {"originalsModified", false}, {"imagesVerified", true}};
}

QVariantMap prepare_gaussian_capture(const QStringList& sources, const QString& project,
                                    int maximum_edge, const std::shared_ptr<std::atomic_bool>& canceled) {
  QVariantList frames;
  QVariantList camera_groups;
  QHash<QString, int> group_indices;
  int prior_frames = 0;
  RenderEditState state;
  const photo_editor::pipeline::CancellationToken cancellation{.cancellation_requested = canceled.get()};
  for (qsizetype index = 0; index < sources.size(); ++index) {
    if (cancellation.cancelled()) return {{"ok", false}, {"error", "Capture preparation canceled."}};
    const auto source = sources[index];
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) return {{"ok", false}, {"error", "Cannot read an original capture."}};
    const QFileInfo before(source);
    auto optics = read_capture_camera_metadata(source);
    auto format = before.suffix().toLower();
    if (format == "jpg") format = "jpeg";
    // RAW and already processed images can have different distortion/crop
    // contracts even when their EXIF camera/lens strings match.
    optics.insert("sourceFormat", format);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!input.atEnd()) {
      if (cancellation.cancelled()) return {{"ok", false}, {"error", "Capture preparation canceled."}};
      const auto bytes = input.read(4 * 1024 * 1024);
      if (bytes.isEmpty()) return {{"ok", false}, {"error", "Cannot hash the complete original capture."}};
      hash.addData(bytes);
    }
    QImage decoded;
    photo_editor::pipeline::DecodedImageMetadata metadata;
    if (!decode_source_image(source, &decoded, maximum_edge, state, &metadata, cancellation))
      return {{"ok", false}, {"error", QStringLiteral("Cannot decode capture: %1").arg(source)}};
    const auto admission = RenderPlanStore::shared().admit(
        QStringLiteral("gaussian-capture:%1").arg(QFileInfo(source).absoluteFilePath()),
        source,
        state,
        decoded.size(),
        photo_editor::pipeline::RenderPurpose::Export,
        photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation,
        std::nullopt,
        &metadata);
    if (!admission.admitted || !admission.plan.has_value()) {
      return {{"ok", false}, {"error", admission.error.isEmpty()
          ? QStringLiteral("Capture could not be admitted for canonical rendering.")
          : admission.error}};
    }
    const auto rendered = render_output_image(
        decoded,
        state,
        QColorSpace(QColorSpace::SRgb),
        QImage::Format_RGB888,
        &metadata,
        nullptr,
        {},
        photo_editor::pipeline::RemovalQuality::Final,
        RenderOutputPolicy::DisplayReferred,
        &*admission.plan,
        cancellation);
    const auto relative = QStringLiteral("images/frame_%1.png").arg(index, 6, 10, QLatin1Char('0'));
    const auto target = QDir(project).filePath(relative);
    const QFileInfo after(source);
    if (rendered.isNull() || cancellation.cancelled() || after.size() != before.size() ||
        after.lastModified() != before.lastModified() || QFileInfo::exists(target) || !rendered.save(target, "PNG"))
      return {{"ok", false}, {"error", "Capture changed or its canonical image could not be saved."}};
    const auto prior = gaussian_camera_prior(optics, rendered.size());
    const bool has_prior = prior.value("available").toBool();
    prior_frames += has_prior ? 1 : 0;
    auto group_key = has_prior ? prior.value("groupKey").toString() : QStringLiteral("estimated");
    if (group_key.isEmpty()) group_key = QStringLiteral("frame-%1").arg(index);
    if (!group_indices.contains(group_key)) {
      group_indices.insert(group_key, static_cast<int>(camera_groups.size()));
      camera_groups.append(QVariantMap{{"prior", prior}, {"images", QStringList{}}});
    }
    const auto group_index = group_indices.value(group_key);
    auto group = camera_groups[group_index].toMap();
    auto group_images = group.value("images").toStringList();
    group_images.append(QFileInfo(relative).fileName());
    group.insert("images", group_images);
    camera_groups[group_index] = group;
    frames.append(QVariantMap{{"sourcePath", source}, {"sourceSha256", QString::fromLatin1(hash.result().toHex())},
        {"sourceBytes", before.size()}, {"preparedPath", relative}, {"width", rendered.width()}, {"height", rendered.height()},
        {"sourceRaw", raw_image_supported(source)}, {"colorSpace", "sRGB"}, {"optics", optics}, {"cameraPrior", prior}});
  }
  for (qsizetype index = 0; index < camera_groups.size(); ++index) {
    auto group = camera_groups[index].toMap();
    const auto list_path = QStringLiteral("camera-group-%1.txt").arg(index, 4, 10, QLatin1Char('0'));
    QSaveFile list(QDir(project).filePath(list_path));
    const auto bytes = (group.value("images").toStringList().join('\n') + '\n').toUtf8();
    if (!list.open(QIODevice::WriteOnly) || list.write(bytes) != bytes.size() || !list.commit())
      return {{"ok", false}, {"error", "Cannot save camera group image list."}};
    group.insert("imageListPath", list_path);
    camera_groups[index] = group;
  }
  return {{"ok", true}, {"frames", frames}, {"frameCount", frames.size()},
      {"cameraGroups", camera_groups}, {"focalPriorFrames", prior_frames},
      {"estimatedIntrinsicsFrames", frames.size() - prior_frames},
      {"colorContract", "canonical-decode-and-render-to-srgb-v1"}, {"originalsModified", false}};
}

QVariantMap prepare_gaussian_timed_capture(const QString& manifest, const QString& new_project,
                                          int maximum_edge, const std::shared_ptr<std::atomic_bool>& canceled) {
  const auto reject = [](const QString& error) { return QVariantMap{{"ok", false}, {"error", error}}; };
  const auto capture = read_gaussian_timed_capture(manifest, canceled);
  if (!capture.valid()) return reject(capture.error);
  const QFileInfo target(QDir::cleanPath(new_project));
  if (!target.isAbsolute() || target.exists() || target.isSymLink() || maximum_edge < 256 || maximum_edge > 4096)
    return reject("Choose a new absolute project folder and an image edge from 256 to 4096.");
  const photo_editor::pipeline::CancellationToken cancellation{.cancellation_requested = canceled.get()};
  QHash<QString, QVariantMap> cameras;
  for (const auto& entry : capture.cameras) cameras.insert(entry.toMap().value("id").toString(), entry.toMap());
  // Validate the calibration's source pixel geometry before creating output.
  for (qsizetype i = 0; i < capture.sources.size(); ++i) {
    if (cancellation.cancelled()) return reject("Timed capture preparation canceled.");
    const auto camera = cameras.value(capture.observations[i].toMap().value("cameraId").toString());
    const auto size = source_image_size(capture.sources[i]);
    if (size != QSize(camera.value("width").toInt(), camera.value("height").toInt()))
      return reject("Camera calibration dimensions do not match the upright source image.");
  }
  if (cancellation.cancelled()) return reject("Timed capture preparation canceled.");
  // Claim the leaf atomically after potentially slow source validation. mkpath
  // alone would also succeed if another operation created the destination in
  // the meantime, allowing its files to be overwritten later in preparation.
  if (!QDir().mkpath(target.absolutePath()) || !QDir(target.absolutePath()).mkdir(target.fileName()) ||
      !QDir(target.absoluteFilePath()).mkdir("images"))
    return reject("Cannot create a new timed capture project; the destination may already exist.");
  const auto prepared = prepare_gaussian_capture(capture.sources, new_project, maximum_edge, canceled);
  if (!prepared.value("ok").toBool()) return prepared;
  const auto frames = prepared.value("frames").toList();
  if (frames.size() != capture.observations.size()) return reject("Prepared observation count does not match the timed capture.");
  QVariantList observations;
  QHash<QString, QSize> prepared_sizes;
  for (qsizetype i = 0; i < frames.size(); ++i) {
    if (cancellation.cancelled()) return reject("Timed capture preparation canceled; partial project retained.");
    const auto frame = frames[i].toMap();
    auto observation = capture.observations[i].toMap();
    const auto id = observation.value("cameraId").toString();
    const QSize size(frame.value("width").toInt(), frame.value("height").toInt());
    if (prepared_sizes.contains(id) && prepared_sizes.value(id) != size)
      return reject("A camera's prepared pixel dimensions changed over time.");
    prepared_sizes.insert(id, size);
    QFile image(QDir(new_project).filePath(frame.value("preparedPath").toString()));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!image.open(QIODevice::ReadOnly) || !hash.addData(&image)) return reject("Cannot verify a prepared timed image.");
    observation.insert("file", frame.value("preparedPath"));
    observation.insert("preparedSha256", QString::fromLatin1(hash.result().toHex()));
    observation.insert("sourcePath", frame.value("sourcePath"));
    observation.insert("sourceSha256", frame.value("sourceSha256"));
    observation.insert("sourceRaw", frame.value("sourceRaw"));
    observations.append(observation);
  }
  QVariantList output_cameras;
  for (const auto& entry : capture.cameras) {
    auto camera = entry.toMap();
    const auto size = prepared_sizes.value(camera.value("id").toString());
    const auto sx = double(size.width()) / camera.value("width").toDouble();
    const auto sy = double(size.height()) / camera.value("height").toDouble();
    camera.insert("sourceWidth", camera.value("width")); camera.insert("sourceHeight", camera.value("height"));
    for (const auto& key : {"fx", "cx"}) camera.insert(key, camera.value(key).toDouble() * sx);
    for (const auto& key : {"fy", "cy"}) camera.insert(key, camera.value(key).toDouble() * sy);
    camera.insert("width", size.width()); camera.insert("height", size.height());
    output_cameras.append(camera);
  }
  const QVariantMap dataset{{"schema", "framehouse-prepared-timed-capture-v1"},
      {"timeUnit", "seconds"}, {"worldUnit", "metres"}, {"cameraConvention", "opencv-world-to-camera"},
      {"pixelConvention", "top-left-corner"}, {"colorContract", prepared.value("colorContract")},
      {"cameras", output_cameras}, {"observations", observations}, {"timeCount", capture.time_count},
      {"durationSeconds", capture.duration_seconds}, {"sourceManifestSha256", capture.manifest_sha256},
      {"state", "prepared-not-trained"}, {"originalsModified", false}};
  const auto bytes = QJsonDocument::fromVariant(dataset).toJson();
  if (bytes.size() > 16 * 1024 * 1024)
    return reject("Prepared timed manifest exceeds 16 MiB. Use a smaller capture; partial images are retained.");
  QSaveFile output(QDir(new_project).filePath("timed-capture.json"));
  if (cancellation.cancelled()) return reject("Timed capture preparation canceled; partial project retained.");
  if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
      cancellation.cancelled() || !output.commit()) return reject("Cannot commit the prepared timed capture manifest.");
  return {{"ok", true}, {"state", "prepared-not-trained"}, {"mode", "timed-4d-input"},
      {"cameraCount", output_cameras.size()}, {"timeCount", capture.time_count}, {"imageCount", observations.size()},
      {"durationSeconds", capture.duration_seconds}, {"manifestPath", output.fileName()},
      {"manifestSha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
      {"trainingAvailable", false}, {"originalsModified", false}};
}
}
