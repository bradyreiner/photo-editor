#include "timeline_desktop_light_field.h"
#include "timeline_vector_path.h"

#include <QImageReader>
#include <QImageWriter>

#include <cstring>

namespace {

bool timeline_generator_images_have_identical_rgba(
    const QImage& expected,
    const QImage& decoded) {
  const auto expected_rgba = expected.convertToFormat(QImage::Format_RGBA8888);
  const auto decoded_rgba = decoded.convertToFormat(QImage::Format_RGBA8888);
  if (expected_rgba.isNull() || decoded_rgba.isNull() ||
      expected_rgba.size() != decoded_rgba.size()) {
    return false;
  }
  const auto row_bytes = static_cast<size_t>(expected_rgba.width()) * 4U;
  for (auto row = 0; row < expected_rgba.height(); ++row) {
    if (std::memcmp(expected_rgba.constScanLine(row), decoded_rgba.constScanLine(row),
                    row_bytes) != 0) {
      return false;
    }
  }
  return true;
}

bool verify_generator_png_round_trip(
    const QString& name,
    const QImage& source,
    const QString& workspace_path) {
  const auto output_path = QDir(workspace_path).filePath(name + QStringLiteral(".png"));
  QElapsedTimer timer;
  timer.start();
  QImageWriter writer(output_path, "PNG");
  writer.setCompression(50);
  if (!writer.write(source)) {
    std::cerr << name.toStdString() << " PNG should encode: "
              << writer.errorString().toStdString() << '\n';
    return false;
  }
  const auto elapsed_milliseconds = timer.elapsed();
  QImageReader reader(output_path, "PNG");
  const auto decoded = reader.read();
  const auto encoded_bytes = QFileInfo(output_path).size();
  if (decoded.isNull() || decoded.size() != source.size() || encoded_bytes <= 0 ||
      !timeline_generator_images_have_identical_rgba(source, decoded)) {
    std::cerr << name.toStdString()
              << " PNG should preserve every decoded RGBA component and alpha value\n";
    return false;
  }
  std::cout << "generator_png_roundtrip=" << name.toStdString()
            << " encode_ms=" << elapsed_milliseconds
            << " bytes=" << encoded_bytes << " exact_rgba=true\n";
  return true;
}

bool verify_generator_frame_cache_png_contract(const QString& workspace_path) {
  using namespace photo_editor::desktop::timeline_motion;
  NativeGeneratorFrameSequenceRequest request;
  request.generator_kind = QStringLiteral("vector-path");
  request.cache_root = QDir(workspace_path).filePath(
      QStringLiteral("native-generator-frame-cache"));
  request.frame_size = QSize(1920, 1080);
  request.frames_per_second = 30.0;
  request.clip_start_seconds = 0.0;
  request.clip_duration_seconds = 1.0;
  request.vector_path_parameters = {
      {QStringLiteral("viewBoxWidth"), 1920.0},
      {QStringLiteral("viewBoxHeight"), 1080.0},
      {QStringLiteral("commands"), QVariantList{
           QVariantMap{{QStringLiteral("verb"), QStringLiteral("M")},
                       {QStringLiteral("points"), QVariantList{128.0, 540.0}}},
           QVariantMap{{QStringLiteral("verb"), QStringLiteral("L")},
                       {QStringLiteral("points"), QVariantList{1792.0, 540.0}}}}},
      {QStringLiteral("stroke"), QStringLiteral("#f2ead8")},
      {QStringLiteral("strokeWidth"), 12.0},
      {QStringLiteral("fill"), QStringLiteral("#00000000")},
  };
  request.samples.push_back({0.5, 0.5, 0.0, 1.0});
  const auto expected_cache_key = native_generator_frame_sequence_cache_key(request);
  const auto rendered = render_vector_path_frame(
      request.frame_size, request.vector_path_parameters, 0.5);
  const auto cached = ensure_native_generator_frame_sequence(request);
  const auto frame_path = QDir(cached.frame_directory).filePath(
      QStringLiteral("frame_000000.png"));
  QImageReader frame_reader(frame_path, "PNG");
  const auto decoded_frame = frame_reader.read();
  if (rendered.isNull() || !cached.ok || cached.cache_hit || cached.frame_count != 1 ||
      cached.cache_key != expected_cache_key ||
      !timeline_generator_images_have_identical_rgba(rendered, decoded_frame)) {
    std::cerr << "1080p native generator cache frame should preserve exact rendered RGBA pixels\n";
    return false;
  }

  // Recreate a pre-compression-change cache entry. The request key and manifest
  // schema are unchanged, so an existing default-compressed PNG should remain reusable.
  if (!rendered.save(frame_path, "PNG")) {
    std::cerr << "Legacy default-compressed generator PNG should encode\n";
    return false;
  }
  const auto manifest_path = QDir(cached.frame_directory).filePath(
      QStringLiteral("manifest.json"));
  QFile manifest_file(manifest_path);
  if (!manifest_file.open(QIODevice::ReadOnly)) {
    std::cerr << "Generator cache manifest should remain readable\n";
    return false;
  }
  auto manifest = QJsonDocument::fromJson(manifest_file.readAll()).object();
  manifest_file.close();
  manifest.insert(QStringLiteral("encodedBytes"), QFileInfo(frame_path).size());
  QSaveFile updated_manifest(manifest_path);
  const auto manifest_bytes = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
  if (!updated_manifest.open(QIODevice::WriteOnly) ||
      updated_manifest.write(manifest_bytes) != manifest_bytes.size() ||
      !updated_manifest.commit()) {
    std::cerr << "Legacy generator cache manifest should be writable in its test fixture\n";
    return false;
  }
  const auto legacy_hit = ensure_native_generator_frame_sequence(request);
  QImageReader legacy_reader(frame_path, "PNG");
  const auto legacy_decoded = legacy_reader.read();
  if (!legacy_hit.ok || !legacy_hit.cache_hit ||
      legacy_hit.cache_key != expected_cache_key ||
      !timeline_generator_images_have_identical_rgba(rendered, legacy_decoded)) {
    std::cerr << "Existing default-compressed generator frames should remain a cache hit\n";
    return false;
  }
  std::cout << "generator_frame_png_cache_contract=passed encode_ms="
            << cached.elapsed_milliseconds << " encoded_bytes=" << cached.encoded_bytes
            << " legacy_cache_hit=true\n";
  return true;
}

bool run_native_generator_frame_png_contract(const QString& workspace_path) {
  const auto generator_light =
      photo_editor::desktop::timeline_motion::render_desktop_light_field(
          QSize(1920, 1080), {.time_seconds = 8.25, .gather = 0.63, .opacity = 1.0});
  const auto generator_veil =
      photo_editor::desktop::timeline_motion::render_transition_veil_frame(
          QSize(1920, 1080), 0.14, 8.25, 0.63, 1.0, 0.42);
  return !generator_light.isNull() && !generator_veil.isNull() &&
         verify_generator_png_round_trip(QStringLiteral("desktop-light-fast-png"),
                                         generator_light, workspace_path) &&
         verify_generator_png_round_trip(QStringLiteral("transition-veil-fast-png"),
                                         generator_veil, workspace_path) &&
         verify_generator_frame_cache_png_contract(workspace_path);
}

}  // namespace

bool run_timeline_generated_source_workflow(const QString& workspace_path) {
  const auto check = [](bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
  };
  TimelineDocumentController controller;
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Native generated source workflow")},
      {QStringLiteral("width"), 128}, {QStringLiteral("height"), 72},
      {QStringLiteral("exportPreset"), QStringLiteral("youtube")},
      {QStringLiteral("frameRate"), 30.0}});
  const QVariantMap path{{"viewBoxWidth", 128.0}, {"viewBoxHeight", 96.0},
      {"commands", QVariantList{
          QVariantMap{{"verb", "M"}, {"points", QVariantList{8.0, 48.0}}},
          QVariantMap{{"verb", "L"}, {"points", QVariantList{120.0, 48.0}}}}},
      {"stroke", "#ffffff"}, {"strokeWidth", 4.0}, {"fill", "#00000000"}};
  const QVector<QPair<QString, QVariantMap>> generators{
      {"desktop-light", {{"timeOffsetSeconds", 0.0}, {"timeScale", 1.0}, {"gather", 0.2}}},
      {"linear-gradient", {{"angleDegrees", 90.0}, {"stops", QVariantList{
          QVariantMap{{"position", 0.0}, {"color", "#080c13"}, {"opacity", 0.6}},
          QVariantMap{{"position", 1.0}, {"color", "#080c13"}, {"opacity", 0.0}}}}}},
      {"fractal-noise", {{"baseFrequencyX", 0.78}, {"baseFrequencyY", 0.78},
                          {"numOctaves", 3}, {"seed", 19}, {"stitchTiles", true},
                          {"opacity", 0.12}}},
      {"transition-veil", {}}, {"vector-path", {{"vectorPath", path}}}};
  QVariantMap fractal_clip;
  QString fractal_track_id;
  for (const auto& [kind, parameters] : generators) {
    const auto track = controller.createTimelineTrackForAutomation("graphics", kind, {});
    if (kind == QStringLiteral("fractal-noise")) {
      fractal_track_id = track.value("trackId").toString();
    }
    const auto created = controller.createTimelineGeneratorForAutomation(
        track.value("trackId").toString(), kind, 0.0, 1.0, parameters);
    if (!created.value("ok").toBool() ||
        !created.value("clip").toMap().value("mediaOnline").toBool()) {
      std::cerr << "Valid native generator must be created online: " << kind.toStdString() << '\n';
      return false;
    }
    if (kind == QStringLiteral("fractal-noise")) {
      fractal_clip = created.value("clip").toMap();
      const QUrl provider_url(fractal_clip.value("generatorSource").toString());
      QSize source_size;
      photo_editor::desktop::timeline_motion::FractalNoiseParameters source_parameters;
      QString parse_error;
      if (!check(provider_url.scheme() == QStringLiteral("image") &&
                     provider_url.host() == QStringLiteral("timeline-fractal-noise") &&
                     photo_editor::desktop::timeline_motion::
                         parse_fractal_noise_image_provider_id(
                             provider_url.path().mid(1), &source_size,
                             &source_parameters, &parse_error) &&
                     source_size == QSize(128, 72) &&
                     source_parameters.base_frequency_x == 0.78 &&
                     source_parameters.base_frequency_y == 0.78 &&
                     source_parameters.seed == 19 &&
                     source_parameters.num_octaves == 3 &&
                     source_parameters.stitch_tiles,
                 "Fractal noise preview URL must encode canonical settings at sequence resolution")) {
        std::cerr << parse_error.toStdString() << '\n';
        return false;
      }
    }
  }
  const auto fractal_clip_id = fractal_clip.value("clipId").toString();
  const auto before_bad_noise = controller.clips().size();
  const auto rejected_noise = controller.createTimelineGeneratorForAutomation(
      fractal_track_id, QStringLiteral("fractal-noise"), 1.0, 1.0,
      {{QStringLiteral("baseFrequencyX"), QStringLiteral("0.78")}});
  if (!check(!rejected_noise.value(QStringLiteral("ok")).toBool() &&
                 controller.clips().size() == before_bad_noise,
             "Fractal-noise validation must reject string frequencies without mutation")) {
    return false;
  }
  if (!check(!fractal_clip_id.isEmpty() && !fractal_track_id.isEmpty() &&
                 controller.setTimelineClipBlendModeForAutomation(
                     fractal_clip_id, QStringLiteral("soft-light"))
                     .value(QStringLiteral("ok")).toBool(),
             "Fractal noise should retain its editable soft-light blend mode")) {
    return false;
  }
  const auto before_save =
      photo_editor::desktop::TimelineHistoryTestAccess::documentObject(controller);
  QJsonObject saved_fractal;
  for (const auto& track_value : before_save.value(QStringLiteral("tracks")).toArray()) {
    for (const auto& clip_value : track_value.toObject().value(QStringLiteral("clips")).toArray()) {
      const auto clip = clip_value.toObject();
      if (clip.value(QStringLiteral("clipId")).toString() == fractal_clip_id) {
        saved_fractal = clip;
      }
    }
  }
  if (!check(!saved_fractal.isEmpty() &&
                 saved_fractal.value(QStringLiteral("generatorKind")).toString() ==
                     QStringLiteral("fractal-noise") &&
                 saved_fractal.value(QStringLiteral("generatorParameters")).toObject()
                         .value(QStringLiteral("seed")).toInt() == 19 &&
                 saved_fractal.value(QStringLiteral("blendMode")).toString() ==
                     QStringLiteral("soft-light"),
             "Fractal noise parameters and soft-light mode should persist in the native graph")) {
    return false;
  }
  if (!controller.saveTimeline(false)) {
    std::cerr << "The fractal-noise timeline should save through the native project pipeline\n";
    return false;
  }
  TimelineDocumentController reopened;
  if (!reopened.loadTimeline(controller.savedDocumentPath())) {
    std::cerr << "The fractal-noise timeline should reopen through the native project loader\n";
    return false;
  }
  const auto reopened_clip = reopened.inspectTimelineClipById(fractal_clip_id);
  const auto reopened_clip_map = reopened_clip.value(QStringLiteral("clip")).toMap();
  if (!check(reopened_clip.value(QStringLiteral("ok")).toBool() &&
                 reopened_clip_map.value(QStringLiteral("generatorKind")).toString() ==
                     QStringLiteral("fractal-noise") &&
                 reopened_clip_map.value(QStringLiteral("blendMode")).toString() ==
                     QStringLiteral("soft-light") &&
                 reopened_clip_map.value(QStringLiteral("generatorParameters")).toMap()
                         .value(QStringLiteral("seed")).toLongLong() == 19 &&
                 reopened_clip_map.value(QStringLiteral("generatorSource")).toString() ==
                     fractal_clip.value(QStringLiteral("generatorSource")).toString(),
             "Saved fractal-noise settings, soft-light mode, and provider identity should survive reopen")) {
    return false;
  }
  const auto shape_track = controller.createTimelineTrackForAutomation("graphics", "shape", {});
  const auto shape = controller.createTimelineGraphicForAutomation(
      shape_track.value("trackId").toString(), "rectangle", 0.0, 1.0,
      {{"fontFamily", "Looksy Missing Font Sentinel"}, {"shapeWidth", 0.1}, {"shapeHeight", 0.1}});
  if (!shape.value("ok").toBool()) {
    std::cerr << "Native shape fixture must be created\n";
    return false;
  }
  for (const auto& row : controller.sequenceTracks())
    for (const auto& item : row.toMap().value("clips").toList())
      if (!item.toMap().value("mediaOnline").toBool()) {
        std::cerr << "Projected native generated clips must stay online without a source file\n";
        return false;
      }
  const auto preflight = controller.timelineDeliveryPreflight();
  if (preflight.value("offlineMediaCount").toInt() != 0 ||
      preflight.value("missingFontCount").toInt() != 0 ||
      !controller.timelinePublishSummary().value("publishEnabled").toBool()) {
    std::cerr << "Native procedural source validity must enable Publish: "
              << QJsonDocument::fromVariant(preflight).toJson().constData() << '\n';
    return false;
  }
  for (const auto& item : preflight.value("issues").toList()) {
    const auto issue = item.toMap();
    if (issue.value("key").toString() == "missing-media" ||
        issue.value("key").toString() == "invalid-generator") {
      std::cerr << "Valid native motion must pass generated-source delivery preflight\n";
      return false;
    }
  }
  const auto output = QDir(workspace_path).filePath("native-generated.mp4");
  if (!reopened.renderCurrentTimeline(output) || QFileInfo(output).size() <= 0) {
    std::cerr << "Native light/gradient/noise/veil/vector timeline must produce a real rendered video\n";
    return false;
  }
  photo_editor::desktop::timeline_motion::FractalNoiseParameters noise_parameters;
  QString noise_error;
  if (!photo_editor::desktop::timeline_motion::
          fractal_noise_parameters_from_variant_map(
              reopened_clip_map.value(QStringLiteral("generatorParameters")).toMap(),
              &noise_parameters, nullptr, &noise_error)) {
    std::cerr << "Reopened fractal-noise parameters should remain valid: "
              << noise_error.toStdString() << '\n';
    return false;
  }
  const auto shared_cache =
      photo_editor::desktop::timeline_motion::ensure_fractal_noise_image_cached(
          QDir(photo_editor::desktop::AppBootstrap::cacheLocation())
              .filePath(QStringLiteral("timeline-fractal-noise")),
          QSize(128, 72), noise_parameters);
  if (!check(shared_cache.ok && shared_cache.cache_hit &&
                 QFileInfo(shared_cache.image_path).isFile(),
             "Preview and export should resolve the same sequence-sized cached noise PNG")) {
    return false;
  }
  if (!run_native_generator_frame_png_contract(workspace_path)) {
    return false;
  }
  std::cout << "Native generated clips stay online and export through the Timeline renderer\n";
  return true;
}
