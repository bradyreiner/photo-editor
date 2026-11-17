#include "edit_recipe_state.h"
#include "photo_layer_effect_codec.h"
#include "timeline_video_grade_runtime.h"

#include <QtEndian>
#include <QJsonDocument>

#include <atomic>
#include <chrono>
#include <thread>

namespace {

bool spatial_grade_run_process(
    const QString& program,
    const QStringList& arguments,
    QString* error = nullptr) {
  QProcess process;
  process.start(program, arguments);
  if (!process.waitForFinished(60000) ||
      process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    if (error != nullptr) {
      *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
    }
    if (process.state() != QProcess::NotRunning) {
      process.kill();
      (void)process.waitForFinished(1000);
    }
    return false;
  }
  return true;
}

QImage spatial_grade_test_image(const QByteArray& pixels, const QSize& size) {
  if (!size.isValid() || pixels.size() != size.width() * size.height() * 8) {
    return {};
  }
  QImage borrowed(
      reinterpret_cast<const uchar*>(pixels.constData()), size.width(),
      size.height(), size.width() * 8, QImage::Format_RGBA64);
  return borrowed.copy();
}

QVariantMap spatial_grade_recipe(const QSize& canvas_size, QString* error) {
  using photo_editor::pipeline::PhotoEffectInstance;
  PhotoEffectInstance blur;
  blur.id = "source-space-masked-blur";
  blur.type_id = "gaussian_blur";
  blur.input_scope =
      photo_editor::pipeline::PhotoEffectInputScope::CompositeBelow;
  blur.gaussian.sigma = 9.0;
  blur.mask_ref = "source-space-off-center-mask";
  const auto encoded_effects = photo_editor::desktop::encode_photo_layer_effects(
      std::span<const PhotoEffectInstance>(&blur, 1), error);
  if (!encoded_effects.has_value()) {
    return {};
  }

  photo_editor::desktop::EditRecipeState recipe;
  recipe.mask_stack = QVariantList{
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("source-space-mask-source")},
          {QStringLiteral("maskSetId"), QStringLiteral("source-space-off-center-mask")},
          {QStringLiteral("name"), QStringLiteral("Off-center blur selection")},
          {QStringLiteral("kind"), QStringLiteral("radial")},
          {QStringLiteral("toolRole"), QStringLiteral("selection")},
          {QStringLiteral("operation"), QStringLiteral("add")},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("centerX"), 0.78},
          {QStringLiteral("centerY"), 0.50},
          {QStringLiteral("radiusX"), 0.30},
          {QStringLiteral("radiusY"), 0.46},
          {QStringLiteral("feather"), 0.10},
          {QStringLiteral("opacity"), 1.0},
          {QStringLiteral("density"), 1.0},
      },
  };
  recipe.layers = QVariantList{
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("source-space-blur-layer")},
          {QStringLiteral("name"), QStringLiteral("Off-center masked blur")},
          {QStringLiteral("kind"), QStringLiteral("adjustment")},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("opacity"), 1.0},
          {QStringLiteral("blendMode"), QStringLiteral("normal")},
          {QStringLiteral("canvasWidth"), canvas_size.width()},
          {QStringLiteral("canvasHeight"), canvas_size.height()},
          {QStringLiteral("effectStack"), *encoded_effects},
      },
  };
  const auto document = QJsonDocument::fromJson(
      photo_editor::desktop::serialize_edit_recipe_json(recipe).toUtf8());
  if (!document.isObject()) {
    if (error != nullptr) {
      *error = QStringLiteral("The spatial grade fixture recipe did not serialize.");
    }
    return {};
  }
  return document.object().toVariantMap();
}

int spatial_grade_opacity_keyframe_count(const QVariantMap& clip) {
  auto count = 0;
  for (const auto& keyframe : clip.value(QStringLiteral("keyframes")).toList()) {
    if (keyframe.toMap().value(QStringLiteral("parameter")).toString() ==
        QStringLiteral("opacity")) {
      ++count;
    }
  }
  return count;
}

bool spatial_grade_save_artifacts(
    const QString& source_directory,
    const QString& output_directory,
    const QJsonObject& report) {
  if (!QDir().mkpath(output_directory)) {
    return false;
  }
  const QStringList names{
      QStringLiteral("spatial-source.png"),
      QStringLiteral("spatial-preview.png"),
      QStringLiteral("spatial-neutral.png"),
      QStringLiteral("spatial-expected.png"),
      QStringLiteral("spatial-delivery.mov"),
      QStringLiteral("spatial-review-excerpt.mp4"),
      QStringLiteral("spatial-delivery-decoded.png"),
      QStringLiteral("spatial-reopened-delivery.mov"),
  };
  for (const auto& name : names) {
    const auto source = QDir(source_directory).filePath(name);
    if (!QFileInfo::exists(source)) {
      continue;
    }
    const auto destination = QDir(output_directory).filePath(name);
    QFile::remove(destination);
    if (!QFile::copy(source, destination)) {
      return false;
    }
  }
  QSaveFile report_file(QDir(output_directory).filePath(
      QStringLiteral("timeline-spatial-source-grade.json")));
  return report_file.open(QIODevice::WriteOnly) &&
         report_file.write(QJsonDocument(report).toJson(QJsonDocument::Indented)) > 0 &&
         report_file.commit();
}

}  // namespace

bool run_timeline_spatial_source_grade_workflow(const QString& workspace_path) {
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (ffmpeg.isEmpty() || ffprobe.isEmpty()) {
    std::cerr << "FFmpeg and ffprobe are required for source-space grade delivery proof\n";
    return false;
  }
  QTemporaryDir temporary_directory(
      QDir(workspace_path).filePath(QStringLiteral("spatial-source-grade-XXXXXX")));
  if (!temporary_directory.isValid()) {
    std::cerr << "could not create the spatial source-grade fixture directory\n";
    return false;
  }
  auto artifact_directory = qEnvironmentVariable(
      "PHOTO_EDITOR_TIMELINE_SPATIAL_GRADE_ARTIFACTS_DIR").trimmed();
  if (artifact_directory.isEmpty()) {
    artifact_directory = QDir::current().filePath(
        QStringLiteral("build/app-audit-fixes-2026-10-25/video/spatial-source-grade"));
  }
  const auto source_path = QDir(temporary_directory.path()).filePath(
      QStringLiteral("spatial-source.mov"));
  const auto still_path = QDir(temporary_directory.path()).filePath(
      QStringLiteral("spatial-source.png"));
  const auto source_size = QSize(160, 90);
  QImage source_image(source_size, QImage::Format_RGB32);
  for (int y = 0; y < source_image.height(); ++y) {
    auto* row = reinterpret_cast<QRgb*>(source_image.scanLine(y));
    for (int x = 0; x < source_image.width(); ++x) {
      const auto stripe = ((x / 5) + (y / 7)) % 2 == 0;
      const auto red = stripe ? 235 : 24;
      const auto green = (x * 3 + y * 2) % 256;
      const auto blue = stripe ? 28 : 222;
      row[x] = qRgb(red, green, blue);
    }
  }
  QString source_generation_error;
  if (!source_image.save(still_path) ||
      !spatial_grade_run_process(
          ffmpeg,
          {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
           QStringLiteral("error"), QStringLiteral("-loop"), QStringLiteral("1"),
           QStringLiteral("-framerate"), QStringLiteral("24"),
           QStringLiteral("-i"), still_path, QStringLiteral("-f"),
           QStringLiteral("lavfi"), QStringLiteral("-i"),
           QStringLiteral("sine=frequency=440:sample_rate=48000:duration=2"),
           QStringLiteral("-t"), QStringLiteral("2"), QStringLiteral("-map"),
           QStringLiteral("0:v:0"), QStringLiteral("-map"),
           QStringLiteral("1:a:0"), QStringLiteral("-c:v"),
           QStringLiteral("prores_ks"), QStringLiteral("-profile:v"),
           QStringLiteral("4"), QStringLiteral("-pix_fmt"),
           QStringLiteral("yuv444p10le"), QStringLiteral("-c:a"),
           QStringLiteral("pcm_s16le"),
           QStringLiteral("-color_range"), QStringLiteral("tv"),
           QStringLiteral("-color_primaries"), QStringLiteral("bt709"),
           QStringLiteral("-color_trc"), QStringLiteral("bt709"),
           QStringLiteral("-colorspace"), QStringLiteral("bt709"),
           QStringLiteral("-y"), source_path},
          &source_generation_error) ||
      !QFileInfo::exists(source_path) || QFileInfo(source_path).size() <= 0) {
    std::cerr << "the spatial grade source fixture could not be generated: "
              << source_generation_error.toStdString() << '\n';
    return false;
  }
  const auto source_stream_probe = process_stdout(
      ffprobe,
      {QStringLiteral("-v"), QStringLiteral("error"),
       QStringLiteral("-select_streams"), QStringLiteral("v:0"),
       QStringLiteral("-show_entries"), QStringLiteral("stream=codec_name,pix_fmt"),
       QStringLiteral("-of"), QStringLiteral("json"), source_path});
  const auto source_streams = QJsonDocument::fromJson(source_stream_probe.toUtf8())
                                 .object()
                                 .value(QStringLiteral("streams")).toArray();
  const auto source_fixture_pixel_format =
      source_streams.isEmpty()
          ? QString{}
          : source_streams.first().toObject()
                .value(QStringLiteral("pix_fmt")).toString();
  if (source_fixture_pixel_format.isEmpty() ||
      !source_fixture_pixel_format.contains(QStringLiteral("444"))) {
    std::cerr << "the spatial-grade fixture should use ProRes 4:4:4 to isolate geometry from 4:2:2 chroma reconstruction; pix_fmt="
              << source_fixture_pixel_format.toStdString() << '\n';
    return false;
  }

  auto controller = TimelineDocumentController{};
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Spatial source-grade proof")},
      {QStringLiteral("width"), 96},
      {QStringLiteral("height"), 96},
      {QStringLiteral("frameRate"), 24.0},
  });
  controller.addClipFromGallery(
      QStringLiteral("video:spatial-source-grade"), source_path,
      QStringLiteral("Spatial source-grade fixture"), 2.0,
      QUrl::fromLocalFile(source_path).toString());
  QElapsedTimer source_probe_timer;
  source_probe_timer.start();
  while (source_probe_timer.elapsed() < 30000 &&
         controller.currentClip().value(QStringLiteral("sourceWidth")).toInt() !=
             source_size.width()) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  const auto clip_id = controller.currentClip()
                           .value(QStringLiteral("clipId")).toString();
  if (clip_id.isEmpty() ||
      controller.currentClip().value(QStringLiteral("sourceWidth")).toInt() !=
          source_size.width() ||
      controller.currentClip().value(QStringLiteral("sourceHeight")).toInt() !=
          source_size.height()) {
    std::cerr << "the spatial source-grade fixture should have analyzed dimensions\n";
    return false;
  }
  const TimelineSourceCrop crop{0.4375, 0.0, 1.0, 1.0};
  const auto crop_rect = crop.pixelRect(source_size);
  if (crop_rect.size() != QSize(90, 90) ||
      !controller.setSelectedSourceCrop(
          crop.left, crop.top, crop.right, crop.bottom)) {
    std::cerr << "the fixture should crop the 16:9 source to an off-center square\n";
    return false;
  }
  controller.setSelectedFrameMode(QStringLiteral("stretch"));
  if (!controller.setDeliverySetting(QStringLiteral("alpha"), true)) {
    std::cerr << "the spatial grade proof should select the high-precision ProRes path\n";
    return false;
  }
  QString recipe_error;
  const auto recipe = spatial_grade_recipe(crop_rect.size(), &recipe_error);
  if (recipe.isEmpty()) {
    std::cerr << "could not prepare the masked blur grade fixture: "
              << recipe_error.toStdString() << '\n';
    return false;
  }
  const auto grade_result = controller.setClipColorGradeRecipeForAutomation(
      clip_id, QJsonObject::fromVariantMap(recipe));
  if (!grade_result.value(QStringLiteral("ok")).toBool()) {
    std::cerr << "the video clip should accept the shared masked blur grade: "
              << grade_result.value(QStringLiteral("error")).toString().toStdString()
              << '\n';
    return false;
  }
  const auto set_keyframe = [&controller, &clip_id](
                                const QString& parameter,
                                double local_seconds,
                                double value) {
    const auto result = controller.editClipKeyframeForAutomation(
        clip_id, QStringLiteral("set"), parameter, local_seconds, value,
        QStringLiteral("linear"));
    return result.value(QStringLiteral("ok")).toBool();
  };
  if (!set_keyframe(QStringLiteral("frameScale"), 0.0, 1.10) ||
      !set_keyframe(QStringLiteral("frameScale"), 2.0, 1.30) ||
      !set_keyframe(QStringLiteral("frameOffsetX"), 0.0, -0.40) ||
      !set_keyframe(QStringLiteral("frameOffsetX"), 2.0, 0.40) ||
      !set_keyframe(QStringLiteral("opacity"), 0.0, 0.35) ||
      !set_keyframe(QStringLiteral("opacity"), 2.0, 0.85)) {
    std::cerr << "the source-grade clip should accept placement and opacity animation\n";
    return false;
  }
  const auto grade_capability =
      controller.inspectClipColorGradeForAutomation(clip_id);
  if (!grade_capability.value(QStringLiteral("ok")).toBool() ||
      !grade_capability.value(QStringLiteral("sequenceExportSupportsGrade")).toBool() ||
      !grade_capability.value(QStringLiteral("sequenceExportUsesSourceBake")).toBool()) {
    std::cerr << "static masked grades on cropped, animated source clips should report the source-bake route; capability="
              << QJsonDocument::fromVariant(grade_capability)
                     .toJson(QJsonDocument::Compact)
                     .toStdString()
              << '\n';
    return false;
  }
  auto rotation_boundary_controller = TimelineDocumentController{};
  rotation_boundary_controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Spatial zoom rotation boundary")},
      {QStringLiteral("width"), 96},
      {QStringLiteral("height"), 96},
      {QStringLiteral("frameRate"), 24.0},
  });
  rotation_boundary_controller.addClipFromGallery(
      QStringLiteral("video:spatial-source-grade-rotation-boundary"), source_path,
      QStringLiteral("Spatial zoom rotation boundary"), 2.0,
      QUrl::fromLocalFile(source_path).toString());
  QElapsedTimer rotation_probe_timer;
  rotation_probe_timer.start();
  while (rotation_probe_timer.elapsed() < 30000 &&
         rotation_boundary_controller.currentClip()
                 .value(QStringLiteral("sourceWidth")).toInt() !=
             source_size.width()) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  const auto rotation_boundary_clip_id =
      rotation_boundary_controller.currentClip()
          .value(QStringLiteral("clipId")).toString();
  const auto rotation_boundary_crop_set =
      !rotation_boundary_clip_id.isEmpty() &&
      rotation_boundary_controller.setSelectedSourceCrop(
          crop.left, crop.top, crop.right, crop.bottom);
  rotation_boundary_controller.setSelectedFrameMode(QStringLiteral("stretch"));
  const auto rotation_boundary_grade =
      rotation_boundary_controller.setClipColorGradeRecipeForAutomation(
          rotation_boundary_clip_id, QJsonObject::fromVariantMap(recipe));
  const auto set_rotation_boundary_keyframe =
      [&rotation_boundary_controller, &rotation_boundary_clip_id](
          const QString& parameter, double seconds, double value) {
        return rotation_boundary_controller.editClipKeyframeForAutomation(
            rotation_boundary_clip_id, QStringLiteral("set"), parameter,
            seconds, value, QStringLiteral("linear"))
            .value(QStringLiteral("ok")).toBool();
      };
  if (rotation_boundary_clip_id.isEmpty() ||
      rotation_boundary_controller.currentClip()
              .value(QStringLiteral("sourceWidth")).toInt() != source_size.width() ||
      !rotation_boundary_crop_set ||
      !rotation_boundary_grade.value(QStringLiteral("ok")).toBool() ||
      !set_rotation_boundary_keyframe(QStringLiteral("frameScale"), 0.0, 1.10) ||
      !set_rotation_boundary_keyframe(QStringLiteral("frameScale"), 2.0, 1.30) ||
      !set_rotation_boundary_keyframe(
          QStringLiteral("frameRotationDegrees"), 0.0, 0.0) ||
      !set_rotation_boundary_keyframe(
          QStringLiteral("frameRotationDegrees"), 2.0, 12.0)) {
    std::cerr << "the rotation boundary fixture should author a cropped grade with animated zoom and rotation\n";
    return false;
  }
  const auto rotation_boundary_capability =
      rotation_boundary_controller.inspectClipColorGradeForAutomation(
          rotation_boundary_clip_id);
  const auto rotation_boundary_error = rotation_boundary_capability
      .value(QStringLiteral("sequenceExportError")).toString();
  if (!rotation_boundary_capability.value(QStringLiteral("ok")).toBool() ||
      rotation_boundary_capability
          .value(QStringLiteral("sequenceExportSupportsGrade")).toBool() ||
      !rotation_boundary_error.contains(QStringLiteral("animated zoom"), Qt::CaseInsensitive) ||
      !rotation_boundary_error.contains(QStringLiteral("rotation"), Qt::CaseInsensitive)) {
    std::cerr << "spatial grade capability should disclose the animated-zoom plus rotation export boundary; capability="
              << QJsonDocument::fromVariant(rotation_boundary_capability)
                     .toJson(QJsonDocument::Compact).toStdString()
              << '\n';
    return false;
  }
  const auto clear_rotation_boundary_grade =
      rotation_boundary_controller.clearClipColorGradeRecipeForAutomation(
          rotation_boundary_clip_id);
  const auto ungraded_rotation_output = QDir(temporary_directory.path()).filePath(
      QStringLiteral("unsupported-animated-zoom-rotation.mov"));
  if (!clear_rotation_boundary_grade.value(QStringLiteral("ok")).toBool() ||
      rotation_boundary_controller.renderCurrentTimeline(ungraded_rotation_output) ||
      QFileInfo::exists(ungraded_rotation_output) ||
      !rotation_boundary_controller.status().contains(
          QStringLiteral("animated zoom"), Qt::CaseInsensitive) ||
      !rotation_boundary_controller.status().contains(
          QStringLiteral("rotation"), Qt::CaseInsensitive)) {
    std::cerr << "timeline export preflight should reject animated zoom with rotation even without a color grade; status="
              << rotation_boundary_controller.status().toStdString() << '\n';
    return false;
  }
  auto preview_input = process_bytes(
      ffmpeg,
      {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
       QStringLiteral("error"), QStringLiteral("-ss"), QStringLiteral("1"),
       QStringLiteral("-i"), source_path, QStringLiteral("-frames:v"),
       QStringLiteral("1"), QStringLiteral("-vf"),
       QStringLiteral("zscale=matrixin=bt709:rangein=limited:transferin=bt709:"
                      "primariesin=bt709:matrix=bt709:range=full:transfer=bt709:"
                      "primaries=bt709,format=rgba64le,") + crop.ffmpegFilter() +
           QStringLiteral(",scale=90:90:eval=frame,format=rgba64le"),
       QStringLiteral("-pix_fmt"), QStringLiteral("rgba64le"),
       QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("pipe:1")});
  auto preview_source = spatial_grade_test_image(preview_input, crop_rect.size());
  if (preview_source.isNull()) {
    std::cerr << "could not decode the cropped source-space preview frame\n";
    return false;
  }
  preview_source.setColorSpace(photo_editor::desktop::timeline_video_rec709_color_space());
  auto grade_session = photo_editor::desktop::create_timeline_video_grade_render_session(
      controller.colorGradeRecipeJsonForClip(clip_id), clip_id, crop_rect.size());
  if (!grade_session.ok()) {
    std::cerr << "could not compile the shared masked blur preview: "
              << grade_session.error.toStdString() << '\n';
    return false;
  }
  const auto compiled_grade =
      photo_editor::desktop::compile_timeline_video_grade_state(
          controller.colorGradeRecipeJsonForClip(clip_id), crop_rect.size());
  if (!compiled_grade.ok()) {
    std::cerr << "could not inspect the canonical masked blur state: "
              << compiled_grade.error.toStdString() << '\n';
    return false;
  }
  auto effect_mask_bound = false;
  auto effect_mask_minimum = 1.0F;
  auto effect_mask_maximum = 0.0F;
  for (const auto& layer : compiled_grade.state->photo_layers) {
    for (const auto& effect : layer.effects) {
      if (effect.mask_ref != "source-space-off-center-mask") {
        continue;
      }
      for (const auto& mask : layer.effect_masks) {
        if (mask.id != effect.mask_ref || mask.coverage.empty()) {
          continue;
        }
        effect_mask_bound = true;
        const auto bounds = std::minmax_element(
            mask.coverage.cbegin(), mask.coverage.cend());
        effect_mask_minimum = *bounds.first;
        effect_mask_maximum = *bounds.second;
      }
    }
  }
  if (!effect_mask_bound ||
      effect_mask_maximum - effect_mask_minimum < 0.1F) {
    std::cerr << "the canonical shared grade state should bind a varying mask to the blur effect; bound="
              << effect_mask_bound << " coverage=[" << effect_mask_minimum
              << ',' << effect_mask_maximum << "]\n";
    return false;
  }
  auto neutral_grade_recipe = photo_editor::desktop::parse_edit_recipe_json(
      controller.colorGradeRecipeJsonForClip(clip_id));
  auto neutralized_masked_effect = false;
  for (auto& layer_value : neutral_grade_recipe.layers) {
    auto layer = layer_value.toMap();
    QString effect_decode_error;
    auto effects = photo_editor::desktop::decode_photo_layer_effects(
        layer, &effect_decode_error);
    if (!effects.has_value()) {
      std::cerr << "could not prepare the neutral grade comparison: "
                << effect_decode_error.toStdString() << '\n';
      return false;
    }
    auto layer_changed = false;
    for (auto& effect : *effects) {
      if (effect.mask_ref != "source-space-off-center-mask") {
        continue;
      }
      effect.enabled = false;
      neutralized_masked_effect = true;
      layer_changed = true;
    }
    if (!layer_changed) {
      continue;
    }
    QString effect_encode_error;
    const auto encoded = photo_editor::desktop::encode_photo_layer_effects(
        std::span<const photo_editor::pipeline::PhotoEffectInstance>(
            effects->data(), effects->size()),
        &effect_encode_error);
    if (!encoded.has_value()) {
      std::cerr << "could not encode the neutral grade comparison: "
                << effect_encode_error.toStdString() << '\n';
      return false;
    }
    layer.insert(QStringLiteral("effectStack"), *encoded);
    layer_value = layer;
  }
  if (!neutralized_masked_effect) {
    std::cerr << "the neutral comparison should disable the linked masked blur effect\n";
    return false;
  }
  const auto neutral_grade_json =
      photo_editor::desktop::serialize_edit_recipe_json(neutral_grade_recipe);
  const auto neutral_grade = photo_editor::desktop::render_timeline_video_grade_frame(
      preview_source, neutral_grade_json, clip_id,
      photo_editor::desktop::timeline_video_rec709_color_space());
  if (!neutral_grade.ok()) {
    std::cerr << "could not render the neutral shared-pipeline comparison: "
              << neutral_grade.error.toStdString() << '\n';
    return false;
  }
  const auto neutral_path = QDir(temporary_directory.path()).filePath(
      QStringLiteral("spatial-neutral.png"));
  if (!neutral_grade.image.save(neutral_path)) {
    std::cerr << "could not save the neutral shared-pipeline comparison\n";
    return false;
  }
  const auto preview_grade = render_timeline_video_grade_frame(
      preview_source, grade_session, QVariantMap{},
      photo_editor::desktop::timeline_video_rec709_color_space());
  if (!preview_grade.ok()) {
    std::cerr << "could not render the shared masked blur preview: "
              << preview_grade.error.toStdString() << '\n';
    return false;
  }
  auto inside_mask_difference = 0.0;
  auto outside_mask_difference = 0.0;
  auto inside_neutral_baseline_difference = 0.0;
  auto outside_neutral_baseline_difference = 0.0;
  auto inside_mask_pixels = 0;
  auto outside_mask_pixels = 0;
  auto mean_rgb_error = 0.0;
  auto maximum_rgb_error = 0.0;
  auto mean_alpha_error = 0.0;
  auto maximum_alpha_error = 0.0;
  auto expected_alpha_mean = 0.0;
  auto actual_alpha_mean = 0.0;
  auto maximum_error_x = -1;
  auto maximum_error_y = -1;
  QColor maximum_error_expected;
  QColor maximum_error_actual;
  for (int y = 0; y < crop_rect.height(); ++y) {
    for (int x = 0; x < crop_rect.width(); ++x) {
      const auto original = preview_source.pixelColor(x, y);
      const auto graded = preview_grade.image.pixelColor(x, y);
      const auto neutral = neutral_grade.image.pixelColor(x, y);
      const auto difference_from_neutral =
          (std::abs(neutral.redF() - graded.redF()) +
           std::abs(neutral.greenF() - graded.greenF()) +
           std::abs(neutral.blueF() - graded.blueF())) / 3.0;
      const auto dx = (static_cast<double>(x) / crop_rect.width() - 0.78) / 0.30;
      const auto dy = (static_cast<double>(y) / crop_rect.height() - 0.50) / 0.46;
      const auto distance = dx * dx + dy * dy;
      if (distance < 0.25) {
        inside_mask_difference += difference_from_neutral;
        inside_neutral_baseline_difference +=
            (std::abs(original.redF() - neutral.redF()) +
             std::abs(original.greenF() - neutral.greenF()) +
             std::abs(original.blueF() - neutral.blueF())) / 3.0;
        ++inside_mask_pixels;
      } else if (distance > 1.8) {
        outside_mask_difference += difference_from_neutral;
        outside_neutral_baseline_difference +=
            (std::abs(original.redF() - neutral.redF()) +
             std::abs(original.greenF() - neutral.greenF()) +
             std::abs(original.blueF() - neutral.blueF())) / 3.0;
        ++outside_mask_pixels;
      }
    }
  }
  inside_mask_difference /= qMax(1, inside_mask_pixels);
  outside_mask_difference /= qMax(1, outside_mask_pixels);
  inside_neutral_baseline_difference /= qMax(1, inside_mask_pixels);
  outside_neutral_baseline_difference /= qMax(1, outside_mask_pixels);
  if (inside_mask_difference < 0.01 ||
      outside_mask_difference > inside_mask_difference * 0.25) {
    std::cerr << "the shared photo grade should blur the off-center mask while preserving pixels outside it; isolatedInsideMean="
              << inside_mask_difference << " isolatedOutsideMean="
              << outside_mask_difference << " insidePixels=" << inside_mask_pixels
              << " outsidePixels=" << outside_mask_pixels << " sourceDeltaInside="
              << inside_neutral_baseline_difference << " sourceDeltaOutside="
              << outside_neutral_baseline_difference << " effectMaskBound="
              << effect_mask_bound << " effectMaskRange=["
              << effect_mask_minimum << ',' << effect_mask_maximum << "]\n";
    return false;
  }

  const auto preview_path = QDir(temporary_directory.path()).filePath(
      QStringLiteral("spatial-preview.png"));
  const auto expected_path = QDir(temporary_directory.path()).filePath(
      QStringLiteral("spatial-expected.png"));
  const auto first_output = QDir(temporary_directory.path()).filePath(
      QStringLiteral("spatial-delivery.mov"));
  const auto spatial_excerpt_parent =
      QFileInfo(temporary_directory.path()).canonicalFilePath();
  const auto spatial_excerpt_path = QDir(spatial_excerpt_parent).filePath(
      QStringLiteral("spatial-review-excerpt.mp4"));
  const auto reopened_output = QDir(temporary_directory.path()).filePath(
      QStringLiteral("spatial-reopened-delivery.mov"));
  if (!preview_grade.image.save(preview_path)) {
    std::cerr << "could not save the spatial grade preview reference\n";
    return false;
  }
  auto preview_rgba64 = preview_grade.image;
  if (preview_rgba64.format() != QImage::Format_RGBA64) {
    preview_rgba64 = preview_rgba64.convertToFormat(QImage::Format_RGBA64);
  }
  const auto preview_raw_path = QDir(temporary_directory.path()).filePath(
      QStringLiteral("spatial-preview.rgba"));
  QFile preview_raw(preview_raw_path);
  const auto row_bytes = preview_rgba64.width() * 8;
  if (preview_rgba64.isNull() || !preview_raw.open(QIODevice::WriteOnly)) {
    std::cerr << "could not prepare the 16-bit preview geometry reference\n";
    return false;
  }
  for (int y = 0; y < preview_rgba64.height(); ++y) {
    if (preview_raw.write(
            reinterpret_cast<const char*>(preview_rgba64.constScanLine(y)),
            row_bytes) != row_bytes) {
      std::cerr << "could not write the 16-bit preview geometry reference\n";
      return false;
    }
  }
  preview_raw.close();
  const auto expected_graph = QStringLiteral(
      "[0:v]scale=96:96:eval=frame,scale=115.2:115.2:eval=frame,"
      "pad='iw+192':'ih+192':96:96:color=black@0:eval=frame,"
      "crop=96:96:'(iw-96)/2':'(ih-96)/2',setsar=1,"
      "colorchannelmixer=aa=0.60000000,format=rgba64le[out]");
  QString expected_graph_error;
  const auto expected_bytes = process_bytes(
      ffmpeg,
      {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
       QStringLiteral("error"), QStringLiteral("-f"),
       QStringLiteral("rawvideo"), QStringLiteral("-pixel_format"),
       QStringLiteral("rgba64le"), QStringLiteral("-video_size"),
       QStringLiteral("%1x%2").arg(preview_rgba64.width()).arg(preview_rgba64.height()),
       QStringLiteral("-framerate"), QStringLiteral("24"),
       QStringLiteral("-i"), preview_raw_path, QStringLiteral("-filter_complex"),
       expected_graph, QStringLiteral("-map"), QStringLiteral("[out]"),
       QStringLiteral("-frames:v"), QStringLiteral("1"),
       QStringLiteral("-pix_fmt"), QStringLiteral("rgba64le"),
       QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("pipe:1")},
      &expected_graph_error);
  const auto expected_image = spatial_grade_test_image(expected_bytes, QSize(96, 96));
  if (expected_image.isNull() || !expected_image.save(expected_path)) {
    std::cerr << "could not render the independent geometry and opacity reference; bytes="
              << expected_bytes.size() << " expected=" << (96 * 96 * 8)
              << " error=" << expected_graph_error.toStdString() << '\n';
    return false;
  }
  const QDir temporary_root(QDir::tempPath());
  const auto temporary_directories_before = temporary_root.entryList(
      {QStringLiteral("photo-editor-spatial-source-*")}, QDir::Dirs);
  const auto source_reference_before_render =
      controller.currentClip().value(QStringLiteral("filePath")).toString();
  if (!controller.renderCurrentTimeline(first_output)) {
    std::cerr << "cropped, placed masked source grade should render: "
              << controller.status().toStdString() << '\n';
    return false;
  }
  QString spatial_excerpt_error;
  if (!controller.renderTimelineReviewExcerpt(
          spatial_excerpt_path, 24, 48, &spatial_excerpt_error)) {
    std::cerr << "a nonzero-start source-grade review excerpt should retain the deferred spatial-grade route: "
              << spatial_excerpt_error.toStdString() << '\n';
    return false;
  }
  QString spatial_excerpt_probe_error;
  const auto spatial_excerpt_probe_text = process_stdout(
      ffprobe,
      {QStringLiteral("-v"), QStringLiteral("error"),
       QStringLiteral("-count_frames"), QStringLiteral("-show_streams"),
       QStringLiteral("-of"), QStringLiteral("json"), spatial_excerpt_path},
      &spatial_excerpt_probe_error);
  QJsonParseError spatial_excerpt_parse_error;
  const auto spatial_excerpt_probe = QJsonDocument::fromJson(
      spatial_excerpt_probe_text.toUtf8(), &spatial_excerpt_parse_error);
  auto spatial_excerpt_video = QJsonObject{};
  for (const auto& stream_value : spatial_excerpt_probe.object()
                                      .value(QStringLiteral("streams")).toArray()) {
    const auto stream = stream_value.toObject();
    if (stream.value(QStringLiteral("codec_type")).toString() ==
        QStringLiteral("video")) {
      spatial_excerpt_video = stream;
      break;
    }
  }
  bool spatial_excerpt_frame_count_ok = false;
  const auto spatial_excerpt_frame_count = spatial_excerpt_video
      .value(QStringLiteral("nb_read_frames")).toString()
      .toLongLong(&spatial_excerpt_frame_count_ok);
  if (!spatial_excerpt_probe_error.isEmpty() ||
      spatial_excerpt_parse_error.error != QJsonParseError::NoError ||
      !spatial_excerpt_probe.isObject() ||
      spatial_excerpt_video.value(QStringLiteral("codec_name")).toString() !=
          QStringLiteral("h264") ||
      spatial_excerpt_video.value(QStringLiteral("width")).toInt() != 96 ||
      spatial_excerpt_video.value(QStringLiteral("height")).toInt() != 96 ||
      !spatial_excerpt_frame_count_ok || spatial_excerpt_frame_count != 24) {
    std::cerr << "nonzero-start source-grade excerpt should retain 24 full-size H.264 frames: "
              << spatial_excerpt_probe_error.toStdString() << '\n';
    return false;
  }
  const auto spatial_excerpt_proof_directory = qEnvironmentVariable(
      "LOOKSY_SPATIAL_REVIEW_EXCERPT_PROOF_DIR").trimmed();
  if (!spatial_excerpt_proof_directory.isEmpty()) {
    if (!QDir().mkpath(spatial_excerpt_proof_directory)) {
      std::cerr << "the spatial review excerpt proof directory could not be created\n";
      return false;
    }
    const auto proof_basename = QStringLiteral("spatial-review-excerpt-%1")
                                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QFile spatial_excerpt_file(spatial_excerpt_path);
    if (!spatial_excerpt_file.open(QIODevice::ReadOnly)) {
      std::cerr << "the deferred-grade review excerpt could not be reopened for proof retention\n";
      return false;
    }
    QSaveFile retained_spatial_excerpt(QDir(spatial_excerpt_proof_directory).filePath(
        proof_basename + QStringLiteral(".mp4")));
    const auto spatial_excerpt_bytes = spatial_excerpt_file.readAll();
    if (!retained_spatial_excerpt.open(QIODevice::WriteOnly) ||
        retained_spatial_excerpt.write(spatial_excerpt_bytes) != spatial_excerpt_bytes.size() ||
        !retained_spatial_excerpt.commit()) {
      std::cerr << "the deferred-grade review excerpt MP4 could not be retained\n";
      return false;
    }
    QSaveFile retained_spatial_probe(QDir(spatial_excerpt_proof_directory).filePath(
        proof_basename + QStringLiteral("-ffprobe.json")));
    const auto spatial_probe_bytes = spatial_excerpt_probe_text.toUtf8();
    if (!retained_spatial_probe.open(QIODevice::WriteOnly) ||
        retained_spatial_probe.write(spatial_probe_bytes) != spatial_probe_bytes.size() ||
        !retained_spatial_probe.commit()) {
      std::cerr << "the deferred-grade review excerpt ffprobe record could not be retained\n";
      return false;
    }
    const QJsonObject spatial_excerpt_verification{
        {QStringLiteral("result"), QStringLiteral("passed")},
        {QStringLiteral("renderRoute"), QStringLiteral("deferred-spatial-grade-frame-stream")},
        {QStringLiteral("startFrame"), 24},
        {QStringLiteral("endFrameExclusive"), 48},
        {QStringLiteral("frameRate"), 24.0},
        {QStringLiteral("decodedVideoFrames"), spatial_excerpt_frame_count},
        {QStringLiteral("videoCodec"), spatial_excerpt_video.value(QStringLiteral("codec_name"))},
        {QStringLiteral("width"), spatial_excerpt_video.value(QStringLiteral("width"))},
        {QStringLiteral("height"), spatial_excerpt_video.value(QStringLiteral("height"))},
        {QStringLiteral("output"), proof_basename + QStringLiteral(".mp4")},
    };
    const auto verification_bytes =
        QJsonDocument(spatial_excerpt_verification).toJson(QJsonDocument::Indented);
    QSaveFile retained_verification(QDir(spatial_excerpt_proof_directory).filePath(
        proof_basename + QStringLiteral("-verification.json")));
    if (!retained_verification.open(QIODevice::WriteOnly) ||
        retained_verification.write(verification_bytes) != verification_bytes.size() ||
        !retained_verification.commit()) {
      std::cerr << "the deferred-grade review excerpt verification record could not be retained\n";
      return false;
    }
    std::cout << "SPATIAL_REVIEW_EXCERPT_PROOF "
              << QDir(spatial_excerpt_proof_directory)
                     .filePath(proof_basename + QStringLiteral(".mp4"))
                     .toStdString()
              << '\n';
  }
  const auto temporary_directories_after = temporary_root.entryList(
      {QStringLiteral("photo-editor-spatial-source-*")}, QDir::Dirs);
  if (temporary_directories_before != temporary_directories_after ||
      controller.currentClip().value(QStringLiteral("filePath")).toString() !=
          source_reference_before_render) {
    std::cerr << "source-grade intermediates should be cleaned and clip source references should remain unchanged; beforeDirs="
              << temporary_directories_before.join(QLatin1Char(',')).toStdString()
              << " afterDirs="
              << temporary_directories_after.join(QLatin1Char(',')).toStdString()
              << " sourceBefore=" << source_reference_before_render.toStdString()
              << " sourceAfter="
              << controller.currentClip().value(QStringLiteral("filePath"))
                     .toString().toStdString()
              << '\n';
    return false;
  }
  const auto audio_stream_duration = [ffprobe](const QString& media_path) {
    const auto output = process_stdout(
        ffprobe,
        {QStringLiteral("-v"), QStringLiteral("error"),
         QStringLiteral("-show_entries"),
         QStringLiteral("stream=codec_type,duration:format=duration"),
         QStringLiteral("-of"), QStringLiteral("json"), media_path});
    const auto document = QJsonDocument::fromJson(output.toUtf8());
    for (const auto& stream_value :
         document.object().value(QStringLiteral("streams")).toArray()) {
      const auto stream = stream_value.toObject();
      if (stream.value(QStringLiteral("codec_type")).toString() !=
          QStringLiteral("audio")) {
        continue;
      }
      bool duration_ok = false;
      const auto duration = stream.value(QStringLiteral("duration")).toString()
                                .toDouble(&duration_ok);
      if (duration_ok && std::isfinite(duration)) {
        return duration;
      }
    }
    return -1.0;
  };
  const auto source_audio_duration = audio_stream_duration(source_path);
  const auto rendered_audio_duration = audio_stream_duration(first_output);
  const auto source_audio_rms_db = detected_channel_mean_volume_db(
      ffmpeg, source_path, 0, 0.25, 0.75);
  const auto rendered_audio_rms_db = detected_channel_mean_volume_db(
      ffmpeg, first_output, 0, 0.25, 0.75);
  if (source_audio_duration < 1.9 || source_audio_duration > 2.1 ||
      rendered_audio_duration < 1.9 || rendered_audio_duration > 2.1 ||
      std::abs(source_audio_duration - rendered_audio_duration) > 0.08 ||
      source_audio_rms_db < -50.0 || rendered_audio_rms_db < -50.0 ||
      std::abs(source_audio_rms_db - rendered_audio_rms_db) > 3.0) {
    std::cerr << "source-grade delivery should retain source audio duration and level; sourceDuration="
              << source_audio_duration << " renderedDuration="
              << rendered_audio_duration << " sourceRmsDb="
              << source_audio_rms_db << " renderedRmsDb="
              << rendered_audio_rms_db << '\n';
    return false;
  }
  const auto rendered_bytes = process_bytes(
      ffmpeg,
      {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
       QStringLiteral("error"), QStringLiteral("-ss"), QStringLiteral("1"),
       QStringLiteral("-i"), first_output, QStringLiteral("-frames:v"),
       QStringLiteral("1"), QStringLiteral("-vf"), QStringLiteral("format=rgba64le"),
       QStringLiteral("-pix_fmt"), QStringLiteral("rgba64le"),
       QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("pipe:1")});
  const auto rendered_image = spatial_grade_test_image(rendered_bytes, QSize(96, 96));
  if (rendered_image.isNull()) {
    std::cerr << "the ProRes spatial-grade output should decode as a 16-bit RGBA frame\n";
    return false;
  }
  auto compared_pixels = 0;
  auto mean_absolute_error = 0.0;
  auto maximum_absolute_error = 0.0;
  auto mean_black_composite_error = 0.0;
  auto maximum_black_composite_error = 0.0;
  auto mean_white_composite_error = 0.0;
  auto maximum_white_composite_error = 0.0;
  for (int y = 0; y < rendered_image.height(); ++y) {
    for (int x = 0; x < rendered_image.width(); ++x) {
      const auto expected = expected_image.pixelColor(x, y);
      const auto actual = rendered_image.pixelColor(x, y);
      const auto red_error = std::abs(expected.redF() - actual.redF());
      const auto green_error = std::abs(expected.greenF() - actual.greenF());
      const auto blue_error = std::abs(expected.blueF() - actual.blueF());
      const auto alpha_error = std::abs(expected.alphaF() - actual.alphaF());
      const auto rgb_error = qMax(red_error, qMax(green_error, blue_error));
      const auto composite_error = [&expected, &actual](double background) {
        const auto expected_alpha = expected.alphaF();
        const auto actual_alpha = actual.alphaF();
        const auto expected_red = expected.redF() * expected_alpha +
                                  background * (1.0 - expected_alpha);
        const auto expected_green = expected.greenF() * expected_alpha +
                                    background * (1.0 - expected_alpha);
        const auto expected_blue = expected.blueF() * expected_alpha +
                                   background * (1.0 - expected_alpha);
        const auto actual_red = actual.redF() * actual_alpha +
                                background * (1.0 - actual_alpha);
        const auto actual_green = actual.greenF() * actual_alpha +
                                  background * (1.0 - actual_alpha);
        const auto actual_blue = actual.blueF() * actual_alpha +
                                 background * (1.0 - actual_alpha);
        return qMax(std::abs(expected_red - actual_red),
                    qMax(std::abs(expected_green - actual_green),
                         std::abs(expected_blue - actual_blue)));
      };
      const auto black_composite_error = composite_error(0.0);
      const auto white_composite_error = composite_error(1.0);
      const auto error = std::max({
          red_error, green_error, blue_error, alpha_error,
      });
      mean_absolute_error += error;
      maximum_absolute_error = qMax(maximum_absolute_error, error);
      mean_black_composite_error += black_composite_error;
      maximum_black_composite_error =
          qMax(maximum_black_composite_error, black_composite_error);
      mean_white_composite_error += white_composite_error;
      maximum_white_composite_error =
          qMax(maximum_white_composite_error, white_composite_error);
      mean_rgb_error += red_error + green_error + blue_error;
      maximum_rgb_error = qMax(maximum_rgb_error, rgb_error);
      mean_alpha_error += alpha_error;
      maximum_alpha_error = qMax(maximum_alpha_error, alpha_error);
      expected_alpha_mean += expected.alphaF();
      actual_alpha_mean += actual.alphaF();
      if (error >= maximum_absolute_error) {
        maximum_error_x = x;
        maximum_error_y = y;
        maximum_error_expected = expected;
        maximum_error_actual = actual;
      }
      ++compared_pixels;
    }
  }
  mean_absolute_error /= qMax(1, compared_pixels);
  mean_black_composite_error /= qMax(1, compared_pixels);
  mean_white_composite_error /= qMax(1, compared_pixels);
  mean_rgb_error /= qMax(1, compared_pixels * 3);
  mean_alpha_error /= qMax(1, compared_pixels);
  expected_alpha_mean /= qMax(1, compared_pixels);
  actual_alpha_mean /= qMax(1, compared_pixels);
  const auto alpha_opacity_valid =
      expected_alpha_mean >= 0.50 && expected_alpha_mean <= 0.70 &&
      actual_alpha_mean >= 0.50 && actual_alpha_mean <= 0.70;
  const auto visible_composite_parity_valid =
      mean_black_composite_error <= 0.035 &&
      maximum_black_composite_error <= 0.20 &&
      mean_white_composite_error <= 0.035 &&
      maximum_white_composite_error <= 0.20;
  const auto raw_rgb_parity_valid =
      mean_rgb_error <= 8.0 / 255.0 && maximum_rgb_error <= 0.20;
  const auto alpha_parity_valid =
      mean_alpha_error <= 0.035 && maximum_alpha_error <= 0.20;
  const auto decoded_output_path = QDir(temporary_directory.path()).filePath(
      QStringLiteral("spatial-delivery-decoded.png"));
  if (!rendered_image.save(decoded_output_path)) {
    std::cerr << "could not save the decoded source-grade output frame\n";
    return false;
  }
  if (!alpha_opacity_valid || !visible_composite_parity_valid ||
      !raw_rgb_parity_valid || !alpha_parity_valid || compared_pixels < 300) {
    const auto failed_report = QJsonObject{
        {QStringLiteral("success"), false},
        {QStringLiteral("route"), QStringLiteral("shared-engine-source-space-bake")},
        {QStringLiteral("sourceSize"), QStringLiteral("160x90")},
        {QStringLiteral("fixtureSourcePixelFormat"), source_fixture_pixel_format},
        {QStringLiteral("chromaQualification"), QStringLiteral("4:4:4 fixture; 4:2:2 preview parity not qualified")},
        {QStringLiteral("crop"), QStringLiteral("x=70,y=0,width=90,height=90")},
        {QStringLiteral("maskInsideMeanDifference"), inside_mask_difference},
        {QStringLiteral("maskOutsideMeanDifference"), outside_mask_difference},
        {QStringLiteral("decodedParityMeanAbsoluteError"), mean_absolute_error},
        {QStringLiteral("decodedParityMaximumAbsoluteError"), maximum_absolute_error},
        {QStringLiteral("blackCompositeMeanMaximumChannelError"), mean_black_composite_error},
        {QStringLiteral("blackCompositeMaximumChannelError"), maximum_black_composite_error},
        {QStringLiteral("whiteCompositeMeanMaximumChannelError"), mean_white_composite_error},
        {QStringLiteral("whiteCompositeMaximumChannelError"), maximum_white_composite_error},
        {QStringLiteral("decodedParityMeanRgbError"), mean_rgb_error},
        {QStringLiteral("decodedParityMaximumRgbError"), maximum_rgb_error},
        {QStringLiteral("decodedParityMeanAlphaError"), mean_alpha_error},
        {QStringLiteral("decodedParityMaximumAlphaError"), maximum_alpha_error},
        {QStringLiteral("expectedAlphaMean"), expected_alpha_mean},
        {QStringLiteral("actualAlphaMean"), actual_alpha_mean},
        {QStringLiteral("alphaOpacityValid"), alpha_opacity_valid},
        {QStringLiteral("visibleCompositeParityValid"), visible_composite_parity_valid},
        {QStringLiteral("rawRgbParityValid"), raw_rgb_parity_valid},
        {QStringLiteral("alphaParityValid"), alpha_parity_valid},
        {QStringLiteral("maximumErrorPixel"),
         QJsonObject{{QStringLiteral("x"), maximum_error_x},
                     {QStringLiteral("y"), maximum_error_y},
                     {QStringLiteral("expected"),
                      QJsonArray{maximum_error_expected.redF(),
                                 maximum_error_expected.greenF(),
                                 maximum_error_expected.blueF(),
                                 maximum_error_expected.alphaF()}},
                     {QStringLiteral("actual"),
                      QJsonArray{maximum_error_actual.redF(),
                                 maximum_error_actual.greenF(),
                                 maximum_error_actual.blueF(),
                                 maximum_error_actual.alphaF()}}}},
        {QStringLiteral("comparedPixels"), compared_pixels},
    };
    if (!spatial_grade_save_artifacts(
            temporary_directory.path(), artifact_directory, failed_report)) {
      std::cerr << "could not retain partial spatial-grade parity diagnostics\n";
    }
    std::cerr << "decoded source-grade delivery should match the shared preview after alpha compositing over black and white, preserve straight RGB, and retain the animated opacity (opacityValid="
              << alpha_opacity_valid << ", mean="
              << mean_absolute_error << ", max=" << maximum_absolute_error
              << ", blackMean=" << mean_black_composite_error
              << ", blackMax=" << maximum_black_composite_error
              << ", whiteMean=" << mean_white_composite_error
              << ", whiteMax=" << maximum_white_composite_error
              << ", rgbMean=" << mean_rgb_error
              << ", alphaMean=" << mean_alpha_error
              << ", expectedAlpha=" << expected_alpha_mean
              << ", actualAlpha=" << actual_alpha_mean
              << ", maxAt=" << maximum_error_x << ',' << maximum_error_y
              << ")\n";
    return false;
  }

  const auto undo_clip_before = controller.currentClip();
  if (spatial_grade_opacity_keyframe_count(undo_clip_before) != 2) {
    std::cerr << "the fixture should retain both animated opacity keyframes\n";
    return false;
  }
  controller.undo();
  if (spatial_grade_opacity_keyframe_count(controller.currentClip()) != 1) {
    std::cerr << "Undo should remove the latest opacity keyframe without removing the grade or crop\n";
    return false;
  }
  controller.redo();
  if (spatial_grade_opacity_keyframe_count(controller.currentClip()) != 2 ||
      controller.currentClip().value(QStringLiteral("sourceCropLeft")).toDouble() !=
          crop.left) {
    std::cerr << "Redo should restore animated opacity and preserve the crop\n";
    return false;
  }
  if (!controller.saveTimeline()) {
    std::cerr << "the source-grade timeline should save\n";
    return false;
  }
  TimelineDocumentController reopened;
  if (!reopened.loadTimeline(controller.savedDocumentPath())) {
    std::cerr << "the spatial source-grade project should reopen\n";
    return false;
  }
  reopened.selectClip(0);
  const auto reopened_clip = reopened.currentClip();
  const auto reopened_grade =
      reopened.inspectClipColorGradeForAutomation(clip_id);
  if (reopened_clip.value(QStringLiteral("filePath")).toString() != source_path ||
      reopened_clip.value(QStringLiteral("sourceWidth")).toInt() != source_size.width() ||
      reopened_clip.value(QStringLiteral("sourceHeight")).toInt() != source_size.height() ||
      spatial_grade_opacity_keyframe_count(reopened_clip) != 2 ||
      !reopened_grade.value(QStringLiteral("sequenceExportUsesSourceBake")).toBool()) {
    std::cerr << "save and reopen should preserve the source path, source crop, animated opacity, and source-bake route\n";
    return false;
  }
  const auto reopened_temporary_before = temporary_root.entryList(
      {QStringLiteral("photo-editor-spatial-source-*")}, QDir::Dirs);
  if (!reopened.renderCurrentTimeline(reopened_output) ||
      temporary_root.entryList({QStringLiteral("photo-editor-spatial-source-*")}, QDir::Dirs) !=
          reopened_temporary_before) {
    std::cerr << "the reopened spatial source-grade project should render and clean its intermediates: "
              << reopened.status().toStdString() << '\n';
    return false;
  }

  const auto long_source_path = QDir(temporary_directory.path()).filePath(
      QStringLiteral("spatial-cancel-source.mov"));
  QString long_source_error;
  if (!spatial_grade_run_process(
          ffmpeg,
          {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
           QStringLiteral("error"), QStringLiteral("-loop"), QStringLiteral("1"),
           QStringLiteral("-framerate"), QStringLiteral("24"),
           QStringLiteral("-i"), still_path, QStringLiteral("-t"),
           QStringLiteral("20"), QStringLiteral("-an"), QStringLiteral("-c:v"),
           QStringLiteral("prores_ks"), QStringLiteral("-profile:v"),
           QStringLiteral("3"), QStringLiteral("-pix_fmt"),
           QStringLiteral("yuv422p10le"),
           QStringLiteral("-color_range"), QStringLiteral("tv"),
           QStringLiteral("-color_primaries"), QStringLiteral("bt709"),
           QStringLiteral("-color_trc"), QStringLiteral("bt709"),
           QStringLiteral("-colorspace"), QStringLiteral("bt709"),
           QStringLiteral("-y"), long_source_path},
          &long_source_error)) {
    std::cerr << "could not prepare the long source for cancellation proof: "
              << long_source_error.toStdString() << '\n';
    return false;
  }
  TimelineDocumentController cancellation_controller;
  cancellation_controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Spatial source-grade cancellation")},
      {QStringLiteral("width"), 96},
      {QStringLiteral("height"), 96},
      {QStringLiteral("frameRate"), 24.0},
  });
  cancellation_controller.addClipFromGallery(
      QStringLiteral("video:spatial-source-grade-cancel"), long_source_path,
      QStringLiteral("Spatial source-grade cancellation fixture"), 20.0,
      QUrl::fromLocalFile(long_source_path).toString());
  QElapsedTimer cancel_source_probe_timer;
  cancel_source_probe_timer.start();
  while (cancel_source_probe_timer.elapsed() < 30000 &&
         cancellation_controller.currentClip()
                 .value(QStringLiteral("sourceWidth")).toInt() !=
             source_size.width()) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  const auto cancel_clip_id = cancellation_controller.currentClip()
                                  .value(QStringLiteral("clipId")).toString();
  QString cancel_recipe_error;
  const auto cancel_recipe = spatial_grade_recipe(crop_rect.size(), &cancel_recipe_error);
  if (cancel_clip_id.isEmpty() ||
      cancellation_controller.currentClip()
              .value(QStringLiteral("sourceWidth")).toInt() != source_size.width() ||
      !cancellation_controller.setSelectedSourceCrop(
          crop.left, crop.top, crop.right, crop.bottom) ||
      !cancellation_controller.setClipColorGradeRecipeForAutomation(
           cancel_clip_id, QJsonObject::fromVariantMap(cancel_recipe))
           .value(QStringLiteral("ok")).toBool()) {
    std::cerr << "the cancellation fixture should admit a static spatial source grade: "
              << cancel_recipe_error.toStdString() << '\n';
    return false;
  }
  auto cancellation_token = std::make_shared<std::atomic_bool>(false);
  cancellation_controller.setRenderCancellationToken(cancellation_token);
  const auto temporary_directories_before_cancel = temporary_root.entryList(
      {QStringLiteral("photo-editor-spatial-source-*")}, QDir::Dirs);
  auto cancellation_render_finished = std::make_shared<std::atomic_bool>(false);
  auto cancellation_watcher = std::thread([
      cancellation_token, cancellation_render_finished, temporary_root,
      temporary_directories_before_cancel]() {
    QElapsedTimer watch_timer;
    watch_timer.start();
    while (watch_timer.elapsed() < 30000 &&
           !cancellation_render_finished->load()) {
      const auto directories = temporary_root.entryList(
          {QStringLiteral("photo-editor-spatial-source-*")}, QDir::Dirs);
      for (const auto& directory : directories) {
        if (temporary_directories_before_cancel.contains(directory)) {
          continue;
        }
        const auto intermediate = QDir(temporary_root.filePath(directory))
                                     .filePath(QStringLiteral("source-grade-0.mkv"));
        if (QFileInfo::exists(intermediate)) {
          std::this_thread::sleep_for(std::chrono::milliseconds(80));
          if (!cancellation_render_finished->load()) {
            cancellation_token->store(true);
          }
          return;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!cancellation_render_finished->load()) {
      cancellation_token->store(true);
    }
  });
  const auto canceled_output = QDir(temporary_directory.path()).filePath(
      QStringLiteral("spatial-canceled.mov"));
  const auto canceled_render_succeeded =
      cancellation_controller.renderCurrentTimeline(canceled_output);
  cancellation_render_finished->store(true);
  cancellation_watcher.join();
  cancellation_controller.setRenderCancellationToken({});
  if (canceled_render_succeeded || !cancellation_token->load() ||
      QFileInfo::exists(canceled_output) ||
      temporary_root.entryList({QStringLiteral("photo-editor-spatial-source-*")}, QDir::Dirs) !=
          temporary_directories_before_cancel) {
    std::cerr << "canceling a source-grade bake should stop delivery and remove its partial intermediate\n";
    return false;
  }

  const auto report = QJsonObject{
      {QStringLiteral("success"), true},
      {QStringLiteral("route"), QStringLiteral("shared-engine-source-space-bake")},
      {QStringLiteral("sourceSize"), QStringLiteral("160x90")},
      {QStringLiteral("fixtureSourcePixelFormat"), source_fixture_pixel_format},
      {QStringLiteral("chromaQualification"), QStringLiteral("4:4:4 fixture; 4:2:2 preview parity not qualified")},
      {QStringLiteral("crop"), QStringLiteral("x=70,y=0,width=90,height=90")},
      {QStringLiteral("maskInsideMeanDifference"), inside_mask_difference},
      {QStringLiteral("maskOutsideMeanDifference"), outside_mask_difference},
      {QStringLiteral("decodedParityMeanAbsoluteError"), mean_absolute_error},
      {QStringLiteral("decodedParityMaximumAbsoluteError"), maximum_absolute_error},
      {QStringLiteral("blackCompositeMeanMaximumChannelError"), mean_black_composite_error},
      {QStringLiteral("blackCompositeMaximumChannelError"), maximum_black_composite_error},
      {QStringLiteral("whiteCompositeMeanMaximumChannelError"), mean_white_composite_error},
      {QStringLiteral("whiteCompositeMaximumChannelError"), maximum_white_composite_error},
      {QStringLiteral("decodedParityMeanRgbError"), mean_rgb_error},
      {QStringLiteral("decodedParityMaximumRgbError"), maximum_rgb_error},
      {QStringLiteral("decodedParityMeanAlphaError"), mean_alpha_error},
      {QStringLiteral("decodedParityMaximumAlphaError"), maximum_alpha_error},
      {QStringLiteral("expectedAlphaMean"), expected_alpha_mean},
      {QStringLiteral("actualAlphaMean"), actual_alpha_mean},
      {QStringLiteral("alphaOpacityValid"), alpha_opacity_valid},
      {QStringLiteral("visibleCompositeParityValid"), visible_composite_parity_valid},
      {QStringLiteral("rawRgbParityValid"), raw_rgb_parity_valid},
      {QStringLiteral("alphaParityValid"), alpha_parity_valid},
      {QStringLiteral("comparedPixels"), compared_pixels},
      {QStringLiteral("savedAndReopened"), true},
      {QStringLiteral("undoAndRedo"), true},
      {QStringLiteral("intermediateCleanup"), true},
      {QStringLiteral("midBakeCancellationCleanup"), true},
  };
  if (!spatial_grade_save_artifacts(temporary_directory.path(), artifact_directory, report)) {
    std::cerr << "could not retain spatial source-grade render evidence\n";
    return false;
  }
  std::cout << "Spatial source-grade artifacts: "
            << artifact_directory.toStdString() << '\n';
  return true;
}
