#include "editor_preview_controller.h"
#include "timeline_color_edit_controller.h"
#include "timeline_document_controller.h"
#include "timeline_playback_controller.h"

#include <QApplication>
#include <QColor>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQuickItem>
#include <QQuickImageProvider>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>

using namespace photo_editor::desktop;

namespace {

bool wait_for(const std::function<bool()>& ready, int timeout_ms = 20000) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < timeout_ms) QTest::qWait(10);
  return ready();
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

QByteArray decode_first_rgb_frame(const QString& ffmpeg, const QString& path) {
  QProcess decode;
  decode.start(ffmpeg, {"-v", "error", "-nostdin", "-i", path,
      "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"});
  if (!decode.waitForFinished(30000) || decode.exitStatus() != QProcess::NormalExit ||
      decode.exitCode() != 0) {
    return {};
  }
  return decode.readAllStandardOutput();
}

double mean_luma_delta(const QImage& source, const QByteArray& output) {
  if (source.isNull() || output.size() != source.width() * source.height() * 3) {
    return 0.0;
  }
  auto total = 0.0;
  const auto* bytes = reinterpret_cast<const uchar*>(output.constData());
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      const auto pixel = source.pixelColor(x, y);
      const auto original_luma = 0.2126 * pixel.red() +
                                 0.7152 * pixel.green() +
                                 0.0722 * pixel.blue();
      const auto index = (y * source.width() + x) * 3;
      const auto output_luma = 0.2126 * bytes[index] +
                               0.7152 * bytes[index + 1] +
                               0.0722 * bytes[index + 2];
      total += std::abs(output_luma - original_luma);
    }
  }
  return total / static_cast<double>(source.width() * source.height());
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir storage;
  if (!storage.isValid()) return fail("create the isolated still-grade fixture directory");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", storage.filePath("app").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", storage.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/isolated_timeline_photo_grade_test");
  qputenv("PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS", "1");
  QApplication app(argc, argv);

  constexpr int kWidth = 96;
  constexpr int kHeight = 64;
  QImage source(kWidth, kHeight, QImage::Format_RGBA8888);
  source.setColorSpace(QColorSpace::SRgb);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      source.setPixelColor(x, y, QColor(
          58 + (x * 2) % 150,
          62 + (y * 2) % 150,
          72 + ((x + y) * 3) % 150));
    }
  }
  const auto source_path = storage.filePath("still-source.png");
  if (!source.save(source_path)) return fail("create the still photo fixture");
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) return fail("FFmpeg is required for Timeline still-photo export");

  TimelineDocumentController document;
  document.createTimelineWithSettings({
      {"timelineName", "Still Photo Grade"},
      {"width", kWidth},
      {"height", kHeight},
      {"frameRate", 12.0},
  });
  document.addClipFromGallery("still-fixture", source_path, "Still fixture", 0.5, {});
  document.selectClip(0);
  const auto clip_id = document.currentClip().value("clipId").toString();
  if (clip_id.isEmpty() || document.currentClip().value("mediaType").toString() != "photo")
    return fail("add the fixture as a native Timeline photo clip");

  TimelinePlaybackController playback(&document);
  QQuickItem video_output;
  video_output.setWidth(kWidth);
  video_output.setHeight(kHeight);
  playback.attachVideoOutput(&video_output);
  TimelineColorEditController bridge(&document);
  bridge.setActive(true);
  if (!wait_for([&] { return bridge.ready(); })) {
    std::cerr << "ready=" << bridge.ready()
              << " loading=" << bridge.loading()
              << " editable=" << bridge.editable()
              << " clip=" << bridge.clipId().toStdString()
              << " error=" << bridge.error().toStdString()
              << " selected=" << document.selectedClipCount()
              << " currentIndex=" << document.currentIndex()
              << " status=" << document.status().toStdString() << '\n';
    return fail("open the shared photo editor on a Timeline still clip");
  }
  auto* editor = qobject_cast<EditorPreviewController*>(bridge.editor());
  if (!editor || !bridge.editable() || editor->catalogRecipePersistenceEnabled())
    return fail("the Timeline still editor must use clip-owned recipe persistence");

  const auto original_recipe = document.colorGradeRecipeJsonForClip(clip_id);
  if (!original_recipe.isEmpty()) return fail("the fixture must start without a grade");
  // Rejection must also restore the private inspector, not leave a phantom Added row.
  if (bridge.addEffect("gaussian_blur") || bridge.error().isEmpty() ||
      !document.colorGradeRecipeJsonForClip(clip_id).isEmpty() || !editor->photoLayers().isEmpty())
    return fail("rejected photo effects must restore the inspector and the exact native recipe");
  const auto before_export = storage.filePath("before.mp4");
  if (!document.renderCurrentTimeline(before_export)) {
    std::cerr << document.status().toStdString() << '\n';
    return fail("export the ungraded native still photo");
  }
  const auto before_pixels = decode_first_rgb_frame(ffmpeg, before_export);
  if (before_pixels.isEmpty()) return fail("decode the ungraded still-photo export");

  editor->setExposure(1.0);
  const auto exposure_from_recipe = [&document, &clip_id]() {
    const auto recipe = QJsonDocument::fromJson(
        document.colorGradeRecipeJsonForClip(clip_id).toUtf8()).object();
    return recipe.value(QStringLiteral("globalAdjustments")).toObject()
        .value(QStringLiteral("exposure")).toDouble();
  };
  if (!wait_for([&] {
        return !document.colorGradeRecipeJsonForClip(clip_id).isEmpty() &&
               std::abs(exposure_from_recipe() - 1.0) < 1.0e-6;
      })) {
    std::cerr << bridge.error().toStdString() << '\n';
    return fail("the actual photo-editor exposure control must commit a static recipe");
  }
  if (!wait_for([&] { return playback.stillPhotoGradePreviewActive(); })) {
    std::cerr << playback.colorGradeError().toStdString() << '\n';
    return fail("the Program preview must publish the graded still through the shared frame pipeline");
  }
  std::unique_ptr<QQuickImageProvider> image_provider(playback.createImageProvider());
  QSize preview_size;
  const auto preview = image_provider->requestImage(QStringLiteral("frame"), &preview_size, {});
  if (preview.isNull() || preview.size() != QSize(kWidth, kHeight) ||
      preview.pixelColor(kWidth / 2, kHeight / 2) == source.pixelColor(kWidth / 2, kHeight / 2)) {
    return fail("the shared Program image must contain visible photo-engine grade pixels");
  }

  const auto graded_recipe = document.colorGradeRecipeJsonForClip(clip_id);
  bridge.undo();
  if (!wait_for([&] { return document.colorGradeRecipeJsonForClip(clip_id) == original_recipe; }) ||
      playback.stillPhotoGradePreviewActive())
    return fail("one Timeline Undo must remove the still grade and return to the source poster");
  bridge.redo();
  if (!wait_for([&] {
        return document.colorGradeRecipeJsonForClip(clip_id) == graded_recipe &&
               playback.stillPhotoGradePreviewActive();
      }))
    return fail("Timeline Redo must restore the still grade and processed Program frame");

  const auto graded_export = storage.filePath("graded.mp4");
  if (!document.renderCurrentTimeline(graded_export)) {
    std::cerr << document.status().toStdString() << '\n';
    return fail("export the graded native still photo through the shared LUT route");
  }
  const auto graded_pixels = decode_first_rgb_frame(ffmpeg, graded_export);
  if (graded_pixels.isEmpty() || mean_luma_delta(source, graded_pixels) < 8.0 ||
      graded_pixels == before_pixels)
    return fail("Timeline export must contain the same visible shared grade response");

  const auto project_path = storage.filePath("still-grade.json");
  if (!document.saveTimelineCopyForAutomation(project_path).value("ok").toBool())
    return fail("save the native still-photo grade recipe");
  bridge.setActive(false);
  TimelineDocumentController reopened;
  if (!reopened.loadTimeline(project_path) ||
      reopened.colorGradeRecipeJsonForClip(clip_id) != graded_recipe)
    return fail("save and reopen must preserve the exact still-photo recipe");
  const auto reopened_export = storage.filePath("reopened.mp4");
  if (!reopened.renderCurrentTimeline(reopened_export))
    return fail("reopened still-photo grade must use the shared export path");
  const auto reopened_pixels = decode_first_rgb_frame(ffmpeg, reopened_export);
  if (reopened_pixels.isEmpty() || reopened_pixels != graded_pixels)
    return fail("save and reopen must preserve identical still-grade export pixels");

  const auto invalid_recipe = QJsonDocument::fromJson(graded_recipe.toUtf8()).object();
  auto spatial_recipe = invalid_recipe;
  auto adjustments = spatial_recipe.value(QStringLiteral("globalAdjustments")).toObject();
  adjustments.insert(QStringLiteral("clarity"), 12.0);
  spatial_recipe.insert(QStringLiteral("globalAdjustments"), adjustments);
  const auto unsupported = document.setClipColorGradeRecipeForAutomation(clip_id, spatial_recipe);
  if (unsupported.value(QStringLiteral("ok")).toBool() ||
      document.colorGradeRecipeJsonForClip(clip_id) != graded_recipe)
    return fail("still photos must reject spatial or image-dependent recipes without mutating the clip");
  const auto animated = document.editClipKeyframeForAutomation(
      clip_id, QStringLiteral("set"), QStringLiteral("grade:exposure"), 0.0, 1.0,
      QStringLiteral("linear"));
  if (animated.value(QStringLiteral("ok")).toBool())
    return fail("still photos must reject animated grade controls");

  const auto raw_path = storage.filePath("unsupported-source.dng");
  QFile raw_file(raw_path);
  if (!raw_file.open(QIODevice::WriteOnly) ||
      raw_file.write("not-a-decoded-camera-raw") <= 0) {
    return fail("create a bounded RAW-source rejection fixture");
  }
  raw_file.close();
  QFile saved_project(project_path);
  if (!saved_project.open(QIODevice::ReadOnly))
    return fail("read the native still-grade project for the RAW rejection fixture");
  auto raw_project = QJsonDocument::fromJson(saved_project.readAll()).object();
  saved_project.close();
  auto raw_tracks = raw_project.value(QStringLiteral("tracks")).toArray();
  auto raw_clip_id = QString{};
  for (qsizetype track_index = 0; track_index < raw_tracks.size(); ++track_index) {
    auto track = raw_tracks.at(track_index).toObject();
    auto clips = track.value(QStringLiteral("clips")).toArray();
    for (qsizetype clip_index = 0; clip_index < clips.size(); ++clip_index) {
      auto clip = clips.at(clip_index).toObject();
      if (clip.value(QStringLiteral("clipId")).toString() != clip_id) continue;
      clip.insert(QStringLiteral("mediaType"), QStringLiteral("photo"));
      clip.insert(QStringLiteral("filePath"), raw_path);
      clip.insert(QStringLiteral("sourceFileSize"),
                  static_cast<double>(QFileInfo(raw_path).size()));
      clips.replace(clip_index, clip);
      track.insert(QStringLiteral("clips"), clips);
      raw_tracks.replace(track_index, track);
      raw_clip_id = clip_id;
      break;
    }
    if (!raw_clip_id.isEmpty()) break;
  }
  if (raw_clip_id.isEmpty())
    return fail("locate the saved photo clip in the native project structure");
  raw_project.insert(QStringLiteral("tracks"), raw_tracks);
  const auto raw_project_path = storage.filePath("unsupported-raw-grade.json");
  QFile raw_project_file(raw_project_path);
  if (!raw_project_file.open(QIODevice::WriteOnly) ||
      raw_project_file.write(QJsonDocument(raw_project).toJson()) <= 0) {
    return fail("write the isolated RAW-grade project fixture");
  }
  raw_project_file.close();
  TimelineDocumentController raw_document;
  if (!raw_document.loadTimeline(raw_project_path))
    return fail("load the isolated RAW-grade project fixture");
  const auto raw_rejected = raw_document.setClipColorGradeRecipeForAutomation(
      raw_clip_id, QJsonDocument::fromJson(graded_recipe.toUtf8()).object());
  if (raw_document.colorGradeRecipeJsonForClip(raw_clip_id).isEmpty() ||
      raw_rejected.value(QStringLiteral("ok")).toBool() ||
      !raw_rejected.value(QStringLiteral("error")).toString().contains(QStringLiteral("RAW-camera decoding"))) {
    return fail("RAW-camera sources must be refused with the explicit unsupported-path reason");
  }

  std::cout << "PASS: native still-photo exposure control, Program preview pixels, Undo/Redo, "
               "pointwise export, save/reopen export pixels, and unsupported-path guards\n";
  return 0;
}
